#include "lifetime.h"
#include <limits>
#include <utility>

namespace HealthyRetirement {
Lifetime::Lifetime(std::uint64_t generation, Identity original, void* renderContext,
                   std::int64_t deadline, Operations operations, Barrier barrier, std::int64_t minimumTime)
  : generation_(generation), original_(original), context_(renderContext), deadline_(deadline),
    barrier_(barrier), ops_(std::move(operations)), lastTime_(minimumTime)
{
  if (!generation_ || !original_.thread || !original_.context || !original_.epoch || !context_ ||
      !ops_.now || !ops_.currentIdentity || !ops_.unregisterCallback || !ops_.invalidateQueued ||
      !ops_.freeContext || !ops_.clearPointer || minimumTime < 0 || deadline_ <= minimumTime ||
      (barrier_ == Barrier::TestQualified && !ops_.barrier)) {
    facts_.phase = Phase::Retained;
    return;
  }
  try { if (!timeValid()) facts_.phase = Phase::Retained; }
  catch (...) { facts_.phase = Phase::Retained; }
}

bool Lifetime::matches(Identity value) const
{
  return value.thread == original_.thread && value.context == original_.context && value.epoch == original_.epoch;
}
bool Lifetime::timeValid() const
{
  if (facts_.phase == Phase::Retained) return false;
  try {
    const auto now = ops_.now();
    if (now >= 0 && now >= lastTime_ && now < deadline_) { lastTime_ = now; return true; }
  } catch (...) {}
  facts_.phase = Phase::Retained;
  return false;
}
Result Lifetime::retain() { facts_.phase = Phase::Retained; return Result::Blocked; }

bool Lifetime::enterRender(std::uint64_t generation)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || facts_.phase != Phase::Active || !ownerValid_) return false;
  }
  Identity current{};
  try { current = ops_.currentIdentity(); } catch (...) { invalidate(generation); return false; }
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != generation_ || facts_.phase != Phase::Active || !ownerValid_) return false;
  if (!timeValid()) return false;
  if (!matches(current)) { retain(); return false; }
  if (facts_.renders == std::numeric_limits<std::uint32_t>::max()) { retain(); return false; }
  ++facts_.renders; return true;
}
void Lifetime::leaveRender()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!facts_.renders) { retain(); return; }
  --facts_.renders;
}
bool Lifetime::enterCallback(std::uint64_t generation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != generation_ || facts_.phase != Phase::Active || !ownerValid_) return false;
  if (!timeValid()) return false;
  if (facts_.callbacks == std::numeric_limits<std::uint32_t>::max()) { retain(); return false; }
  ++facts_.callbacks; return true;
}
void Lifetime::leaveCallback()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!facts_.callbacks) { retain(); return; }
  --facts_.callbacks;
}
bool Lifetime::deliveryAllowed(std::uint64_t generation) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return generation == generation_ && facts_.phase == Phase::Active && ownerValid_ && timeValid();
}
bool Lifetime::enterDelivery(std::uint64_t generation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != generation_ || facts_.phase != Phase::Active || !ownerValid_) return false;
  if (!timeValid()) return false;
  if (facts_.deliveries == std::numeric_limits<std::uint32_t>::max()) { retain(); return false; }
  ++facts_.deliveries; return true;
}
void Lifetime::leaveDelivery()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!facts_.deliveries) { retain(); return; }
  --facts_.deliveries;
}
Result Lifetime::request(std::uint64_t generation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != generation_ || facts_.phase != Phase::Active || !ownerValid_) return Result::Blocked;
  try { if (!timeValid()) return retain(); } catch (...) { return retain(); }
  facts_.admissionClosed = true;
  facts_.phase = Phase::Closing;
  return Result::Pending;
}

Result Lifetime::retireOnRenderThread(std::uint64_t generation)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || facts_.phase != Phase::Closing || retiring_ || !ownerValid_)
      return Result::Blocked;
    try { if (!timeValid()) return retain(); } catch (...) { return retain(); }
    if (facts_.renders) return Result::Pending;
    retiring_ = true;
  }
  Result result = Result::Blocked;
  try { result = retireSteps(); } catch (...) {
    std::lock_guard<std::mutex> lock(mutex_);
    retain();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  retiring_ = false;
  return result;
}

Result Lifetime::retireSteps()
{
  if (!matches(ops_.currentIdentity())) { std::lock_guard<std::mutex> lock(mutex_); return retain(); }
  bool unregister = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
    unregister = !facts_.unregisterInvoked;
    if (unregister) facts_.unregisterInvoked = true;
  }
  if (unregister) {
    ops_.unregisterCallback(context_);
    std::lock_guard<std::mutex> lock(mutex_);
    facts_.unregisterReturned = true;
    if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
  }
  if (barrier_ == Barrier::Unqualified) { std::lock_guard<std::mutex> lock(mutex_); return retain(); }
  bool qualified = false;
  { std::lock_guard<std::mutex> lock(mutex_); qualified = facts_.barrierQualified; }
  if (!qualified) {
    const auto barrier = ops_.barrier();
    if (barrier == BarrierResult::Waiting) {
      std::lock_guard<std::mutex> lock(mutex_);
      if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
      return Result::Pending;
    }
    if (barrier != BarrierResult::Ready) { std::lock_guard<std::mutex> lock(mutex_); return retain(); }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
    facts_.barrierQualified = true;
  }
  bool invalidate = false;
  { std::lock_guard<std::mutex> lock(mutex_); invalidate = !facts_.queuedInvalidated; }
  if (invalidate && !ops_.invalidateQueued(generation_)) {
    std::lock_guard<std::mutex> lock(mutex_); return retain();
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    facts_.queuedInvalidated = true;
    if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
    if (facts_.callbacks || facts_.renders || facts_.deliveries) return Result::Pending;
  }
  if (!matches(ops_.currentIdentity())) { std::lock_guard<std::mutex> lock(mutex_); return retain(); }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (facts_.phase != Phase::Closing || !ownerValid_ || !timeValid()) return retain();
    facts_.phase = Phase::Freeing;
    facts_.freeInvoked = true;
  }
  ops_.freeContext(context_);
  { std::lock_guard<std::mutex> lock(mutex_); facts_.freeReturned = true; }
  bool identityAfter = false;
  try { identityAfter = matches(ops_.currentIdentity()); } catch (...) {}
  const bool cleared = ops_.clearPointer(context_);
  std::lock_guard<std::mutex> lock(mutex_);
  facts_.pointerCleared = cleared;
  if (!cleared || !identityAfter || facts_.phase != Phase::Freeing || !ownerValid_ || !timeValid()) return retain();
  ack_.emplace(Ack{generation_, original_, lastTime_});
  facts_.ackPublished = true;
  facts_.phase = Phase::Acknowledged;
  return Result::Done;
}

void Lifetime::invalidate(std::uint64_t generation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != generation_) return;
  ownerValid_ = false;
  retain();
}
std::optional<Ack> Lifetime::ack(std::uint64_t generation) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    if (generation == generation_ && ownerValid_ && facts_.phase == Phase::Acknowledged && timeValid()) return ack_;
  } catch (...) {}
  return std::nullopt;
}
bool Lifetime::acceptAck(std::uint64_t generation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  try {
    if (generation == generation_ && ownerValid_ && facts_.phase == Phase::Acknowledged &&
        !facts_.ackAccepted && timeValid() && ack_) {
      facts_.ackAccepted = true; return true;
    }
  } catch (...) {}
  return false;
}
bool Lifetime::mayDestroy(std::uint64_t generation, bool eventWorkerClosed) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  try { return eventWorkerClosed && generation == generation_ && ownerValid_ && facts_.phase == Phase::Acknowledged && facts_.ackAccepted && timeValid(); }
  catch (...) { return false; }
}
Facts Lifetime::facts() const { std::lock_guard<std::mutex> lock(mutex_); return facts_; }

Slot::Slot(std::uint64_t generation, std::int64_t deadline, std::function<std::int64_t()> clock)
  : generation_(generation), deadline_(deadline), clock_(std::move(clock))
{
  try {
    if (!generation_ || !clock_ || (lastTime_ = clock_()) < 0 || deadline_ <= lastTime_) closed_ = true;
  } catch (...) { closed_ = true; }
}

bool Slot::beginCreation()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || creating_ || lifetime_ || !generation_ || !clock_) return false;
  try {
    const auto now = clock_();
    if (now < lastTime_ || now < 0 || now >= deadline_) { closed_ = true; return false; }
    lastTime_ = now;
  }
  catch (...) { closed_ = true; return false; }
  creating_ = true;
  return true;
}

bool Slot::beforeCreate()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!creating_ || closed_ || !clock_) return false;
  try {
    const auto now = clock_();
    if (now >= 0 && now >= lastTime_ && now < deadline_) { lastTime_ = now; return true; }
  } catch (...) {}
  closed_ = true;
  return false;
}

bool Slot::recordCreated(void* context)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!context || retainedContext_) { closed_ = true; return false; }
  retainedContext_ = context;
  if (!creating_) { closed_ = true; return false; }
  return !closed_;
}

bool Slot::hasContextRecord() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return retainedContext_ != nullptr;
}

std::shared_ptr<Lifetime> Slot::install(Identity original, void* context, Operations operations)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!creating_ || lifetime_ || !generation_ || !context || context != retainedContext_) return {};
  creating_ = false;
  operations.now = clock_;
  lifetime_ = std::make_shared<Lifetime>(generation_, original, context, deadline_, std::move(operations),
                                         Barrier::Unqualified, lastTime_);
  if (closed_) lifetime_->invalidate(generation_);
  return lifetime_;
}

void Slot::creationFailed()
{
  std::shared_ptr<Lifetime> lifetime;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    creating_ = false;
    closed_ = true;
    lifetime = lifetime_;
  }
  if (lifetime) lifetime->invalidate(generation_);
}

Result Slot::request(std::uint64_t generation)
{
  std::shared_ptr<Lifetime> lifetime;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || closed_) return Result::Blocked;
    closed_ = true;
    lifetime = lifetime_;
  }
  return lifetime ? lifetime->request(generation) : Result::Blocked;
}

std::shared_ptr<Lifetime> Slot::current() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return lifetime_;
}

void Slot::invalidate(std::uint64_t generation)
{
  if (generation != generation_) return;
  std::shared_ptr<Lifetime> lifetime;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    closed_ = true;
    lifetime = lifetime_;
  }
  if (lifetime) lifetime->invalidate(generation);
}
}
