#include "owner.h"
#include <utility>
#include <vector>

namespace HealthyRetirement {
namespace {
// Process-lifetime containment intentionally has no destructor. Uncertain
// native work can still refer to every member of the graph at process exit.
struct Held { std::unique_ptr<Graph> graph; std::shared_ptr<Slot> slot; std::shared_ptr<void> token; };
void contain(std::unique_ptr<Graph> graph, std::shared_ptr<Slot> slot, std::shared_ptr<void> token)
{
  static auto* mutex = new std::mutex;
  static auto* held = new std::vector<Held>;
  std::lock_guard<std::mutex> lock(*mutex);
  held->push_back({std::move(graph), std::move(slot), std::move(token)});
}
}

RenderJob::RenderJob(std::shared_ptr<Owner> owner) : owner_(std::move(owner)) {}
RenderJob::~RenderJob() { if (!ran_ && owner_) owner_->droppedJob(); }
void RenderJob::run()
{
  if (ran_) return;
  ran_ = true;
  if (owner_) owner_->runJob();
}

Owner::Owner(std::unique_ptr<Graph> graph, std::shared_ptr<Slot> slot,
             std::shared_ptr<void> token, std::int64_t deadline, Clock clock, Schedule schedule)
  : graph_(std::move(graph)), slot_(std::move(slot)), token_(std::move(token)),
    deadline_(deadline), clock_(std::move(clock)), schedule_(std::move(schedule))
{
  if (!graph_ || !slot_ || !token_ || !clock_ || !schedule_ || !graph_->intact() || !validTime()) fail();
}
Owner::~Owner()
{
  if (graph_) contain(std::move(graph_), std::move(slot_), std::move(token_));
}
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
  if (slot_) slot_->invalidate(slot_->generation());
}
bool Owner::request()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (requested_ || failed_ || released_ || !graph_->intact() || !validTime()) { fail(); return false; }
    requested_ = true;
    if (slot_->request(slot_->generation()) != Result::Pending) { fail(); return false; }
  }
  return post();
}
bool Owner::post()
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_ || pending_ || !graph_->intact() || !validTime()) { fail(); return false; }
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
    if (failed_ || released_ || !pending_ || !graph_->intact() || !validTime()) { fail(); return; }
    lifetime = slot_->current();
    if (!lifetime) { fail(); return; }
  }
  const auto result = lifetime->retireOnRenderThread(slot_->generation());
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
    if (!graph_->intact() || !validTime()) { fail(); return; }
    const auto lifetime = slot_->current();
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
  if (!released_) fail();
}
bool Owner::acceptAndRelease()
{
  std::unique_ptr<Graph> graph;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (failed_ || released_ || pending_ || !graph_->intact() || !validTime()) { fail(); return false; }
    const auto lifetime = slot_->current();
    if (!lifetime || !graph_->eventWorkerClosed()) return false;
    const auto generation = slot_->generation();
    if (!lifetime->acceptAck(generation) || !lifetime->mayDestroy(generation, true) ||
        !graph_->approveDestruction()) { fail(); return false; }
    released_ = true;
    graph = std::move(graph_);
  }
  graph.reset();
  token_.reset();
  return true;
}
bool Owner::retained() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return !released_;
}
}
