#include "graph_retirement.h"
#include <utility>

namespace HealthyRetirement {
GraphRetirement::GraphRetirement(std::shared_ptr<Slot> slot, std::shared_ptr<Owner> owner,
    std::shared_ptr<GraphAccess> access, std::int64_t deadline, Boundaries boundaries)
  : slot_(std::move(slot)), owner_(std::move(owner)), access_(std::move(access)),
    deadline_(deadline), boundaries_(std::move(boundaries)) {}

#ifdef HEALTHY_RETIREMENT_TESTING
std::shared_ptr<GraphRetirement> GraphRetirement::forTest(std::shared_ptr<Slot> slot,
    std::shared_ptr<Owner> owner, std::shared_ptr<Lifetime> lifetime, std::shared_ptr<GraphAccess> access,
    std::int64_t deadline, Boundaries boundaries)
{
  auto result = std::shared_ptr<GraphRetirement>(new GraphRetirement(
      std::move(slot), std::move(owner), std::move(access), deadline, std::move(boundaries)));
  result->testLifetime_ = std::move(lifetime);
  return result;
}
#endif
std::shared_ptr<Lifetime> GraphRetirement::lifetime() const
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (testLifetime_) return testLifetime_;
#endif
  return slot_ ? slot_->current() : nullptr;
}
bool GraphRetirement::request()
{
  if (!slot_ || !owner_ || !access_ || stop_ || !boundaries_.capability || !boundaries_.closure.settledStop ||
      !boundaries_.stopClaim || !boundaries_.bindStopClaim || !boundaries_.bindScheduler) return false;
  try {
    if (!boundaries_.capability(slot_->generation())) { owner_->invalidate(); return false; }
    const auto stop = boundaries_.closure.settledStop();
    const auto claim = boundaries_.stopClaim();
    if (!stop) return false;
    if (stop->origin != Closure::StopKey::Origin::InternalReplacement || !stop->host || !stop->core ||
        !stop->epoch || !stop->request || !stop->revision || !stop->attempt || !claim ||
        claim->generation() != slot_->generation() || !claim->open()) {
      owner_->invalidate(); return false;
    }
    if (!boundaries_.bindStopClaim(claim)) { owner_->invalidate(); return false; }
    stop_ = stop;
    claim_ = claim;
    boundaries_.bindScheduler();
    if (owner_->request()) return true;
  } catch (...) {}
  owner_->invalidate();
  return false;
}
FixtureGraph::Progress GraphRetirement::poll()
{
  const auto self = shared_from_this();
  bool valid = false;
  try { valid = slot_ && boundaries_.capability && boundaries_.capability(slot_->generation()); } catch (...) {}
  if (!owner_ || !slot_ || !stop_ || !claim_ || !claim_->eligible() || !valid) {
    if (owner_) owner_->invalidate();
    return FixtureGraph::Progress::Failed;
  }
  owner_->poll();
  const auto state = lifetime();
  if (!state || state->facts().phase == Phase::Retained) return FixtureGraph::Progress::Failed;
  if (!state->ack(slot_->generation())) return FixtureGraph::Progress::Pending;
  if (!closure_) {
    try {
#ifdef HEALTHY_RETIREMENT_TESTING
      if (testLifetime_)
        closure_ = std::make_shared<Closure>(slot_, testLifetime_, slot_->generation(), deadline_, *stop_, claim_, boundaries_.closure);
      else
#endif
        closure_ = std::make_shared<Closure>(slot_, slot_->generation(), deadline_, *stop_, claim_, boundaries_.closure);
      if (!closure_->start()) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
    } catch (...) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
  }
  closure_->poll();
  if (closure_->facts().phase == Closure::Phase::Retained) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
  if (!closure_->closed(slot_->generation())) return FixtureGraph::Progress::Pending;
  return publish();
}
bool GraphRetirement::closed() const
{
  return closure_ && slot_ && closure_->closed(slot_->generation());
}
std::optional<ClosedAck> GraphRetirement::ack() const { return readAck(access_); }
std::optional<ClosedAck> GraphRetirement::readAck(const std::shared_ptr<GraphAccess>& access)
{
  if (!access) return std::nullopt;
  std::lock_guard<std::mutex> lock(access->mutex);
  try {
    if (access->revoked.load() || !access->closed || !access->capability || !access->stopClaim || !access->stopClaim->eligible() ||
        !access->capability(access->closed->generation) || !access->clock) return std::nullopt;
    const auto now = access->clock();
    if (now < access->closed->completedAt || now >= access->deadline) return std::nullopt;
    return access->closed;
  } catch (...) { return std::nullopt; }
}
FixtureGraph::Progress GraphRetirement::publish()
{
  const auto generation = slot_->generation();
  const auto clock = boundaries_.closure.now;
  if (!clock) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
  std::int64_t before;
  try { before = clock(); } catch (...) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
  if (before < 0 || before >= deadline_) { owner_->invalidate(); return FixtureGraph::Progress::Failed; }
  if (!owner_->acceptAndRelease()) return FixtureGraph::Progress::Failed;
  try {
    const auto now = clock();
    if (now < before || now >= deadline_ || !claim_->eligible() || !boundaries_.capability(generation))
      return FixtureGraph::Progress::Failed;
    std::lock_guard<std::mutex> lock(access_->mutex);
    access_->capability = boundaries_.capability;
    access_->stopClaim = claim_;
    access_->clock = clock;
    access_->deadline = deadline_;
    access_->closed.emplace(ClosedAck{generation, now});
    return FixtureGraph::Progress::Closed;
  } catch (...) { return FixtureGraph::Progress::Failed; }
}
}
