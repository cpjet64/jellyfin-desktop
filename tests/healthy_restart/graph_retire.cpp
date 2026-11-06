#include "graph_retirement.h"
#include "graph_access.h"
#include "closure_native.h"
#include "module_channel.h"
#include "owner_qt.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include <QThread>
#include <stdexcept>

namespace HealthyRetirement {
namespace {
Closure::StopKey closureKey(const HealthyRestart::AsyncStop::Key& key)
{
  return {Closure::StopKey::Origin::InternalReplacement, key.host, key.core, key.epoch,
          key.request, key.revision, key.attempt, key.entry, key.paused};
}
}
bool FixtureGraph::installRetirementClaim()
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !containment ||
      !containment->intact() || !player_ || !owner_ || std::atomic_load(&access_->intentClaim)) return false;
  try {
    auto claim = std::make_shared<RetirementClaim>(slot_->generation());
    std::atomic_store(&access_->intentClaim, claim);
    if (access_->revoked.load()) claim->revoke();
    if (!claim->open() || !player_->healthyInstallRetirementClaim(claim, slot_->generation())) {
      claim->revoke(); owner_->invalidate(); return false;
    }
    if (!access_->revoked.load()) return true;
    claim->revoke(); owner_->invalidate(); return false;
  } catch (...) { owner_->invalidate(); return false; }
}
bool FixtureGraph::requestRetirement()
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !player_ ||
      !containment || !containment->intact() || !owner_ || !module_ || retirementRequested_ || !scheduler_) return false;
  try {
    const QPointer<PlayerComponent> player = player_.get();
    const QPointer<MpvVideoItem> item = item_;
    native_ = std::make_shared<NativeCell>();
    const auto native = native_;
    const auto module = module_;
    const auto scheduler = scheduler_.get();
    const auto owner = owner_;
    GraphRetirement::Boundaries bounds;
    bounds.capability = GraphAccessOps::capability(access_, [module](std::uint64_t generation) {
      return module->stillValid(generation);
    });
    bounds.bindScheduler = [scheduler, owner] { scheduler->bind(owner); };
    const auto claimAccess = access_;
    bounds.stopClaim = [player, claimAccess]() -> std::shared_ptr<const RetirementClaim> {
      const auto claim = player ? player->healthyStopClaim() : nullptr;
      return claim && claim == std::atomic_load(&claimAccess->intentClaim) ? claim : nullptr;
    };
    bounds.bindStopClaim = [slot = slot_](std::shared_ptr<const RetirementClaim> claim) {
      return slot->bindStopClaim(std::move(claim));
    };
    bounds.closure.now = [clock = clock_, module, generation = slot_->generation()] {
      return module->stillValid(generation) ? clock() : std::int64_t{-1};
    };
    bounds.closure.settledStop = [player]() -> std::optional<Closure::StopKey> {
      if (!player) return std::nullopt;
      const auto current = player->healthyCompletedStop();
      return current ? std::optional<Closure::StopKey>{closureKey(*current)} : std::nullopt;
    };
    bounds.closure.closeEvents = [player] {
      return player ? player->healthyCloseEvents() : Closure::EventResult::Failed;
    };
    bounds.closure.quitWorker = [item, native] {
      if (!item || !(native->state = item->healthyTakeNative()))
        throw std::runtime_error("native ownership transfer failed");
      native->state->quit();
    };
    bounds.closure.joinWorker = [native](std::int64_t remaining) {
      return native->state && native->state->wait(remaining);
    };
    bounds.closure.destroyCore = [native] {
      if (!native->state) throw std::runtime_error("native state unavailable");
      native->state->destroy();
    };
    retirement_ = std::shared_ptr<GraphRetirement>(new GraphRetirement(slot_, owner_, access_, deadline_, std::move(bounds)));
    if (!retirement_->request()) return false;
    retirementRequested_ = true;
    return true;
  } catch (...) { owner_->invalidate(); return false; }
}
FixtureGraph::Progress FixtureGraph::pollRetirement()
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !retirement_ ||
      !containment || !containment->intact()) {
    if (owner_) owner_->invalidate();
    return Progress::Failed;
  }
  const auto retirement = retirement_;
  return retirement->poll();
}
void FixtureGraph::invalidate() noexcept
{
  access_->revoked.store(true);
  if (const auto claim = std::atomic_load(&access_->intentClaim)) claim->revoke();
  if (retirement_ && retirement_->closure()) retirement_->closure()->invalidate();
  const auto containment = containment_.lock();
  if (containment && containment->intact() && player_) player_->cancelHealthyReplacementStop();
  if (owner_) owner_->invalidate();
  else if (containment) containment->markLost();
}
}
