#include "closure.h"
#include <exception>
#include <QThread>
#include <utility>

namespace HealthyRetirement {
namespace {
bool sameStop(const Closure::StopKey& a, const Closure::StopKey& b)
{
  return a.origin == b.origin && a.host == b.host && a.core == b.core && a.epoch == b.epoch && a.request == b.request &&
    a.revision == b.revision && a.attempt == b.attempt && a.entry == b.entry && a.paused == b.paused;
}
}
Closure::Closure(std::shared_ptr<Slot> slot, std::uint64_t generation,
                 std::int64_t deadline, StopKey stop, std::shared_ptr<const RetirementClaim> lease,
                 Operations operations)
  : slot_(std::move(slot)), generation_(generation), deadline_(deadline), stop_(stop),
    lease_(std::move(lease)), ops_(std::move(operations))
{
  facts_.generation = generation_;
}
#ifdef HEALTHY_RETIREMENT_TESTING
Closure::Closure(std::shared_ptr<Slot> slot, std::shared_ptr<Lifetime> testLifetime,
                 std::uint64_t generation, std::int64_t deadline, StopKey stop,
                 std::shared_ptr<const RetirementClaim> lease, Operations operations)
  : Closure(std::move(slot), generation, deadline, stop, std::move(lease), std::move(operations))
{
  testLifetime_ = std::move(testLifetime);
}
#endif

Closure::~Closure()
{
  // Destruction while native work remains is a graph ownership violation.
  if (worker_) std::terminate();
}

bool Closure::validTime()
{
  if (invalidated_.load() || !lease_ || lease_->generation() != generation_ || !lease_->eligible() || !ops_.now)
    return false;
  auto lifetime = slot_ ? slot_->current() : nullptr;
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testLifetime_) lifetime = testLifetime_;
#endif
  if (!lifetime || !lifetime->matchesClaim(lease_) || !lifetime->ack(generation_)) return false;
  const auto now = ops_.now();
  if (now < lastTime_ || now >= deadline_) return false;
  lastTime_ = now;
  return true;
}

void Closure::invalidate() noexcept { invalidated_.store(true); }

bool Closure::start()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (facts_.phase != Phase::Initial || polling_) return false;
    polling_ = true;
  }
  try {
    auto lifetime = slot_ ? slot_->current() : nullptr;
#ifdef HEALTHY_RETIREMENT_TESTING
    if (testLifetime_) lifetime = testLifetime_;
#endif
    const auto stop = ops_.settledStop ? ops_.settledStop() : std::nullopt;
    std::lock_guard<std::mutex> lock(mutex_);
    polling_ = false;
    if (!generation_ || !slot_ ||
        slot_->generation() != generation_ || !lifetime || !lifetime->ack(generation_) ||
        !stop || stop_.origin != StopKey::Origin::InternalReplacement || !stop_.host || !stop_.core ||
        !stop_.epoch || !stop_.request || !stop_.revision || !stop_.attempt ||
        !sameStop(*stop, stop_) ||
        !ops_.closeEvents || !ops_.quitWorker || !ops_.joinWorker || !ops_.destroyCore || !validTime())
      return false;
    facts_.phase = Phase::ClosingEvents;
    return true;
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    polling_ = false;
    facts_.phase = Phase::Retained;
    return false;
  }
}

void Closure::poll()
{
  // A completed run() does not prove TLS cleanup finished. This zero-time
  // join is outside the mutex and can never wait on the GUI thread.
  QThread* completed = nullptr;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (polling_) return;
    if (completionReturned_.load() && worker_) { polling_ = true; completed = worker_; }
  }
  if (completed) {
    const bool settled = completed->wait(0);
    std::lock_guard<std::mutex> lock(mutex_);
    polling_ = false;
    if (!settled) return;
    delete worker_;
    worker_ = nullptr;
    try {
      if (!validTime()) { facts_.phase = Phase::Retained; return; }
      if (facts_.phase == Phase::Destroying && facts_.destroyReturned) facts_.phase = Phase::Closed;
    } catch (...) { facts_.phase = Phase::Retained; }
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (polling_) return;
    if (completionReturned_.load()) return;
    if (facts_.phase != Phase::ClosingEvents) return;
    try { if (!validTime()) { facts_.phase = Phase::Retained; return; } }
    catch (...) { facts_.phase = Phase::Retained; return; }
    polling_ = true;
  }
  EventResult result = EventResult::Failed;
  try {
    const auto stop = ops_.settledStop();
    if (stop && sameStop(*stop, stop_)) result = ops_.closeEvents();
  } catch (...) {}
  {
    std::lock_guard<std::mutex> lock(mutex_);
    try {
      if (result == EventResult::Failed || !validTime()) { facts_.phase = Phase::Retained; polling_ = false; return; }
      if (result == EventResult::Pending) { polling_ = false; return; }
      facts_.eventsClosed = true;
      facts_.quitRequested = true;
    } catch (...) { facts_.phase = Phase::Retained; polling_ = false; return; }
  }
  try { ops_.quitWorker(); } catch (...) { result = EventResult::Failed; }
  std::lock_guard<std::mutex> lock(mutex_);
  polling_ = false;
  try {
    if (result == EventResult::Failed || !validTime()) { facts_.phase = Phase::Retained; return; }
    facts_.phase = Phase::Joining;
    worker_ = QThread::create([this] { complete(); });
    worker_->start();
  } catch (...) { facts_.phase = Phase::Retained; }
}

void Closure::complete()
{
  try {
    std::int64_t remaining;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!validTime()) { facts_.phase = Phase::Retained; completionReturned_ = true; return; }
      remaining = deadline_ - lastTime_;
    }
    const bool joined = ops_.joinWorker(remaining);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!joined || !validTime()) { facts_.phase = Phase::Retained; completionReturned_ = true; return; }
      facts_.joined = true;
      facts_.destroyInvoked = true;
      facts_.phase = Phase::Destroying;
    }
    ops_.destroyCore();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      facts_.destroyReturned = true;
      if (!validTime()) facts_.phase = Phase::Retained;
    }
  } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    facts_.phase = Phase::Retained;
  }
  completionReturned_ = true;
}

Closure::Facts Closure::facts() const { std::lock_guard<std::mutex> lock(mutex_); return facts_; }
bool Closure::closed(std::uint64_t generation) const
{
  const auto value = facts();
  auto lifetime = slot_ ? slot_->current() : nullptr;
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testLifetime_) lifetime = testLifetime_;
#endif
  return generation == generation_ && lease_ && lease_->generation() == generation_ && lease_->eligible() && lifetime &&
      lifetime->matchesClaim(lease_) &&
      lifetime->ack(generation_) && value.phase == Phase::Closed;
}
}
