#include "paused_hold.h"
#include "capture_capability.h"
#include <cstdlib>
#include <utility>

namespace HealthyRestart {
namespace {
constexpr std::int64_t holdNs = 3000000000LL;
constexpr std::int64_t maxGapNs = 250000000LL;
bool sameBinding(const PausedHoldBinding& a, const PausedHoldBinding& b)
{
  return a.frame == b.frame && a.host == b.host && a.revision == b.revision &&
      a.attempt == b.attempt && a.grant == b.grant && a.checkpointMs == b.checkpointMs &&
      a.deadlineNs == b.deadlineNs && a.claim && a.claim == b.claim;
}
}

PausedHoldProof::PausedHoldProof(PausedHoldBinding binding, fixture_restart::Capture baseline, fixture_restart::Capture target,
                                 std::shared_ptr<const std::atomic_bool> validity, std::int64_t lastRequestNs,
                                 std::int64_t completedNs)
  : binding_(std::move(binding)), baseline_(std::move(baseline)), target_(std::move(target)), validity_(std::move(validity)),
    lastRequestNs_(lastRequestNs), completedNs_(completedNs) {}

bool PausedHoldProof::matches(const PausedHoldBinding& binding, std::int64_t nowNs) const
{
  return retained() && sameBinding(binding_, binding) && nowNs >= completedNs_ &&
      nowNs < binding_.deadlineNs && nowNs >= lastRequestNs_ && nowNs - lastRequestNs_ <= maxGapNs;
}

bool PausedHoldProof::retained() const
{
  return baseline_ && baseline_->captureEvidence &&
      baseline_->captureEvidence->matches(baseline_->key, binding_.attempt, false, baseline_->serial) &&
      target_ && target_->captureEvidence &&
      target_->captureEvidence->matches(target_->key, binding_.attempt, true, target_->serial) &&
      validity_ && validity_->load() && binding_.claim && binding_.claim->open();
}

PausedHold::PausedHold(PausedHoldBinding binding) : binding_(std::move(binding)) {}
PausedHold::~PausedHold() { invalidate(); }

bool PausedHold::live(std::int64_t nowNs) const
{
  const auto& f = binding_.frame;
  return !invalid_ && nowNs >= 0 && nowNs < binding_.deadlineNs && binding_.claim &&
      binding_.claim->open() && binding_.host && binding_.revision && binding_.attempt && binding_.grant &&
      f.process && f.core && f.context && f.entry && f.phase && f.seek &&
      binding_.checkpointMs >= 2900 && binding_.checkpointMs <= 3100;
}

bool PausedHold::baseline(fixture_restart::Capture record)
{
  if (armed_ || invalid_ || !record || !record->captureEvidence ||
      !record->captureEvidence->matches(record->key, binding_.attempt, false, record->serial)) { invalidate(); return false; }
  baseline_ = std::make_shared<const fixture_restart::FrameRecord>(*record);
  if (!frames_.baseline(baseline_)) { invalidate(); return false; }
  return true;
}

bool PausedHold::arm(std::uint64_t afterSerial, std::int64_t armedNs)
{
  if (armed_ || !live(armedNs) || !baseline_ || !baseline_->captureEvidence ||
      !baseline_->captureEvidence->matches(baseline_->key, binding_.attempt, false, baseline_->serial) ||
      !frames_.arm(binding_.frame, afterSerial, static_cast<int>(binding_.checkpointMs))) {
    invalidate(); return false;
  }
  armedNs_ = armedNs; armed_ = true; return true;
}

void PausedHold::capture(fixture_restart::Capture record)
{
  if (!armed_ || invalid_ || !record || !record->captureEvidence ||
      !record->captureEvidence->matches(record->key, binding_.attempt, true, record->serial) ||
      record->capturedAt < static_cast<std::uint64_t>(armedNs_)) return;
  frames_.capture(std::move(record));
}

bool PausedHold::observe(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs,
                         std::int64_t positionMs)
{
  if (!armed_ || !live(receivedNs) || !id || id <= lastId_ || requestedNs < armedNs_ ||
      receivedNs < requestedNs || receivedNs - requestedNs > maxGapNs ||
      (lastId_ && (requestedNs < lastReceivedNs_ || receivedNs - lastRequestNs_ > maxGapNs)) ||
      positionMs < 0 || std::llabs(positionMs - binding_.checkpointMs) > 250 ||
      (!lastId_ && std::llabs(positionMs - binding_.checkpointMs) > 100) ||
      (lastId_ && std::llabs(positionMs - firstPositionMs_) > 250)) {
    invalidate(); return false;
  }
  if (!lastId_) { firstReceivedNs_ = receivedNs; firstPositionMs_ = positionMs; }
  lastId_ = id; lastRequestNs_ = requestedNs; lastReceivedNs_ = receivedNs;
  frames_.acknowledge(binding_.frame, true, true, true);
  return true;
}

std::shared_ptr<const PausedHoldProof> PausedHold::finish(std::int64_t nowNs)
{
  const auto target = frames_.target();
  if (!armed_ || !live(nowNs) || !lastId_ || nowNs < lastReceivedNs_ ||
      nowNs - lastRequestNs_ > maxGapNs || lastRequestNs_ - firstReceivedNs_ < holdNs ||
      !target || !target->captureEvidence ||
      !target->captureEvidence->matches(target->key, binding_.attempt, true, target->serial) ||
      !baseline_ || !baseline_->captureEvidence ||
      !baseline_->captureEvidence->matches(baseline_->key, binding_.attempt, false, baseline_->serial) ||
      target->capturedAt > static_cast<std::uint64_t>(nowNs)) return {};
  return std::shared_ptr<const PausedHoldProof>(new PausedHoldProof(binding_, baseline_, target, validity_, lastRequestNs_, nowNs));
}

void PausedHold::invalidate()
{
  validity_->store(false); invalid_ = true; armed_ = false; frames_.invalidate(); baseline_.reset();
}
}
