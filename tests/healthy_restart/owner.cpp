#include "owner.h"
#include <utility>

namespace HealthyRetirement {
struct Containment::State {
  std::unique_ptr<Graph> graph;
  std::shared_ptr<Slot> slot;
  std::shared_ptr<void> token;
};
Containment::Containment() : state_(std::make_unique<State>()) {}
bool Containment::adopt(std::unique_ptr<Graph>& graph, const std::shared_ptr<Slot>& slot,
                        const std::shared_ptr<void>& token) noexcept
{
  if (!state_ || adopted_ || !graph || !slot || !token) return false;
  adopted_ = true;
  state_->graph = std::move(graph);
  state_->slot = slot;
  state_->token = token;
  intact_.store(true);
  return true;
}
Containment::~Containment() { if (state_ && state_->graph) (void)state_.release(); }

RenderJob::RenderJob(std::shared_ptr<Owner> owner) : owner_(std::move(owner)) {}
RenderJob::~RenderJob() { if (!ran_ && owner_) owner_->droppedJob(); }
void RenderJob::run()
{
  if (ran_) return;
  ran_ = true;
  if (owner_) owner_->runJob();
}

Owner::Owner(std::shared_ptr<Containment> containment, std::int64_t deadline, Clock clock, Schedule schedule)
  : containment_(std::move(containment)), deadline_(deadline), clock_(std::move(clock)), schedule_(std::move(schedule))
{
  if (!containment_ || containment_->claimed_.exchange(true)) { failed_ = true; return; }
  claimed_ = true;
  if (!containment_->intact() || !clock_ || !schedule_ || !validTime()) fail();
}
#ifdef HEALTHY_RETIREMENT_TESTING
Owner::Owner(std::shared_ptr<Containment> containment, std::int64_t deadline, Clock clock,
             Schedule schedule, std::shared_ptr<Lifetime> testLifetime)
  : Owner(std::move(containment), deadline, std::move(clock), std::move(schedule))
{
  testLifetime_ = std::move(testLifetime);
  if (!testLifetime_) fail();
}
#endif
std::shared_ptr<Lifetime> Owner::lifetime() const
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testLifetime_) return testLifetime_;
#endif
  return containment_->state_->slot->current();
}
Owner::~Owner() = default;
bool Owner::validTime()
{
  try {
    const auto now = clock_();
    if (now >= 0 && now >= lastTime_ && now < deadline_) { lastTime_ = now; return true; }
  } catch (...) {}
  return false;
}
void Owner::fail()
{
  failed_ = true;
  if (!claimed_ || !containment_ || !containment_->state_ || !containment_->state_->slot) return;
  const auto slot = containment_->state_->slot;
  slot->invalidate(slot->generation());
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testLifetime_) testLifetime_->invalidate(slot->generation());
#endif
}
bool Owner::request()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requested_ || failed_ || released_ || !containment_->intact() || !validTime()) { fail(); return false; }
    requested_ = true;
    const auto slot = containment_->state_->slot;
#ifdef HEALTHY_RETIREMENT_TESTING
    const auto result = testLifetime_ ? testLifetime_->request(slot->generation()) : slot->request(slot->generation());
#else
    const auto result = slot->request(slot->generation());
#endif
    if (result != Result::Pending) { fail(); return false; }
  }
  return post();
}
bool Owner::post()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_ || pending_ || !containment_->intact() || !validTime()) { fail(); return false; }
    pending_ = true;
  }
  try {
    auto job = std::make_shared<RenderJob>(shared_from_this());
    if (schedule_(job)) return true;
  } catch (...) {}
  invalidate();
  return false;
}
void Owner::runJob()
{
  std::shared_ptr<Lifetime> lifetime;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_ || !pending_ || !containment_->intact() || !validTime()) { fail(); return; }
    lifetime = this->lifetime();
    if (!lifetime) { fail(); return; }
  }
  const auto result = lifetime->retireOnRenderThread(containment_->state_->slot->generation());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = false;
    if (result == Result::Blocked) fail();
  }
  // Pending is reposted by poll on the owner thread; never block the render thread.
}
void Owner::droppedJob() { invalidate(); }
void Owner::poll()
{
  bool repost = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_) return;
    if (!containment_->intact() || !validTime()) { fail(); return; }
    const auto lifetime = this->lifetime();
    if (!lifetime) { fail(); return; }
    const auto phase = lifetime->facts().phase;
    if (phase == Phase::Retained) { fail(); return; }
    repost = requested_ && !pending_ && phase == Phase::Closing;
  }
  if (repost) post();
}
void Owner::invalidate()
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!released_ && claimed_) { containment_->markLost(); fail(); }
}
bool Owner::acceptAndRelease()
{
  std::unique_ptr<Graph> graph;
  Graph* candidate = nullptr;
  std::shared_ptr<Lifetime> state;
  std::uint64_t generation = 0;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (accepting_ || failed_ || released_ || pending_) return false;
    if (!containment_->intact() || !validTime()) { fail(); return false; }
    state = lifetime();
    if (!state) { fail(); return false; }
    generation = containment_->state_->slot->generation();
    candidate = containment_->state_->graph.get();
    accepting_ = true;
  }
  bool workerClosed = false;
  try { workerClosed = candidate->eventWorkerClosed(); } catch (...) { invalidate(); }
  if (!workerClosed) {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_ || !containment_->intact() || containment_->state_->graph.get() != candidate || !validTime() ||
        !state->acceptAck(generation) || !state->mayDestroy(generation, true)) {
      accepting_ = false; fail(); return false;
    }
  }
  bool approved = false;
  try { approved = candidate->approveDestruction(); } catch (...) {}
  {
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_ = false;
    if (!approved || failed_ || released_ || !containment_->intact() || containment_->state_->graph.get() != candidate || !validTime() ||
        !state->mayDestroy(generation, true)) { fail(); return false; }
    released_ = true;
    graph = std::move(containment_->state_->graph);
  }
  graph.reset();
  containment_->state_->token.reset();
  return true;
}
bool Owner::retained() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return !released_;
}
bool Owner::renderThreadIs(std::uintptr_t thread) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto state = containment_ && containment_->state_ && containment_->state_->slot ? lifetime() : nullptr;
  return state && thread && state->originalIdentity().thread == thread;
}
}
