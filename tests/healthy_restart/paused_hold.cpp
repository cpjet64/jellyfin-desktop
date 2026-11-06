#include "paused_hold.h"
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

PausedHoldProof::PausedHoldProof(PausedHoldBinding binding, fixture_restart::Capture target,
                                 std::shared_ptr<const std::atomic_bool> validity, std::int64_t lastRequestNs,
                                 std::int64_t completedNs)
  : binding_(std::move(binding)), target_(std::move(target)), validity_(std::move(validity)),
    lastRequestNs_(lastRequestNs), completedNs_(completedNs) {}

bool PausedHoldProof::matches(const PausedHoldBinding& binding, std::int64_t nowNs) const
{
  return retained() && sameBinding(binding_, binding) && nowNs >= completedNs_ &&
      nowNs < binding_.deadlineNs && nowNs >= lastRequestNs_ && nowNs - lastRequestNs_ <= maxGapNs;
}

bool PausedHoldProof::retained() const
{
  return target_ && validity_ && validity_->load() && binding_.claim && binding_.claim->open();
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
  if (armed_ || invalid_ || !record || !frames_.baseline(std::move(record))) { invalidate(); return false; }
  return true;
}

bool PausedHold::arm(std::uint64_t afterSerial, std::int64_t armedNs)
{
  if (armed_ || !live(armedNs) || !frames_.arm(binding_.frame, afterSerial, static_cast<int>(binding_.checkpointMs))) {
    invalidate(); return false;
  }
  armedNs_ = armedNs; armed_ = true; return true;
}

void PausedHold::capture(fixture_restart::Capture record)
{
  if (!armed_ || invalid_ || !record || record->capturedAt < static_cast<std::uint64_t>(armedNs_)) return;
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
      !target || target->capturedAt > static_cast<std::uint64_t>(nowNs)) return {};
  return std::shared_ptr<const PausedHoldProof>(new PausedHoldProof(binding_, target, validity_, lastRequestNs_, nowNs));
}

void PausedHold::invalidate()
{
  validity_->store(false); invalid_ = true; armed_ = false; frames_.invalidate();
}
}
