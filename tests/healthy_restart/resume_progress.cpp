#include "resume_progress.h"
#include <limits>
#include <utility>

namespace HealthyRestart {
namespace {
constexpr std::int64_t secondNs = 1000000000LL;
constexpr std::int64_t windowNs = 10 * secondNs;
bool sameBinding(const PausedHoldBinding& a, const PausedHoldBinding& b)
{
  return a.frame == b.frame && a.host == b.host && a.revision == b.revision && a.attempt == b.attempt &&
      a.grant == b.grant && a.checkpointMs == b.checkpointMs && a.deadlineNs == b.deadlineNs &&
      a.claim && a.claim == b.claim;
}
}

ResumeProgressProof::ResumeProgressProof(PausedHoldBinding binding, std::shared_ptr<const PausedHoldProof> hold,
    fixture_restart::Capture first, fixture_restart::Capture second, std::shared_ptr<const std::atomic_bool> validity,
    std::int64_t completedNs)
  : binding_(std::move(binding)), hold_(std::move(hold)), first_(std::move(first)), second_(std::move(second)),
    validity_(std::move(validity)), completedNs_(completedNs) {}

bool ResumeProgressProof::matches(const PausedHoldBinding& binding, std::int64_t nowNs) const
{
  return validity_ && validity_->load() && hold_ && hold_->retained() && first_ && second_ &&
      sameBinding(binding_, binding) && nowNs >= completedNs_ && nowNs < binding_.deadlineNs;
}

ResumeProgress::ResumeProgress(PausedHoldBinding binding, std::shared_ptr<const PausedHoldProof> hold,
                               std::uint64_t afterSerial, std::int64_t dispatchNs)
  : binding_(std::move(binding)), hold_(std::move(hold)), boundary_(afterSerial), dispatchNs_(dispatchNs)
{
  if (!hold_ || !hold_->matches(binding_, dispatchNs_)) invalidate();
}

ResumeProgress::~ResumeProgress() { invalidate(); }

bool ResumeProgress::live(std::int64_t nowNs) const
{
  return !invalid_ && hold_ && hold_->retained() && binding_.claim && binding_.claim->open() &&
      dispatchNs_ >= 0 && nowNs >= dispatchNs_ && nowNs < binding_.deadlineNs && nowNs - dispatchNs_ <= windowNs;
}

void ResumeProgress::capture(fixture_restart::Capture record)
{
  if (!record || record->capturedAt > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return;
  const auto time = static_cast<std::int64_t>(record->capturedAt);
  if (!live(time) || !(record->key == binding_.frame) || record->serial <= boundary_ ||
      record->thread != hold_->target_->thread || record->actualContext != hold_->target_->actualContext) return;
  const auto index = fixture_restart::decodeFrame(*record);
  if (!index) return;
  if (!first_) { first_ = std::make_shared<const fixture_restart::FrameRecord>(*record); return; }
  const auto firstIndex = fixture_restart::decodeFrame(*first_);
  if (!second_ && firstIndex && *index > *firstIndex && record->serial > first_->serial &&
      record->capturedAt >= first_->capturedAt && record->capturedAt - first_->capturedAt >= secondNs &&
      record->rgba != first_->rgba)
    second_ = std::make_shared<const fixture_restart::FrameRecord>(*record);
}

bool ResumeProgress::initial(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs,
                             std::int64_t positionMs)
{
  if (lastObservationId_ || !id || requestedNs < dispatchNs_ || receivedNs < requestedNs ||
      !live(receivedNs) || positionMs < 0 || positionMs > 10000) { invalidate(); return false; }
  lastObservationId_ = id; lastReceivedNs_ = receivedNs; initialPositionMs_ = positionMs;
  return true;
}

bool ResumeProgress::advance(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs,
                             std::int64_t positionMs)
{
  if (!lastObservationId_ || id <= lastObservationId_ || requestedNs < lastReceivedNs_ ||
      receivedNs < requestedNs || !live(receivedNs) || positionMs < initialPositionMs_ || positionMs > 10000) {
    invalidate(); return false;
  }
  lastObservationId_ = id; lastReceivedNs_ = receivedNs;
  advanced_ |= positionMs - initialPositionMs_ >= 1000;
  return advanced_;
}

std::shared_ptr<const ResumeProgressProof> ResumeProgress::finish(std::int64_t nowNs)
{
  if (!live(nowNs) || !advanced_ || !first_ || !second_ || nowNs < lastReceivedNs_ ||
      second_->capturedAt > static_cast<std::uint64_t>(nowNs)) return {};
  return std::shared_ptr<const ResumeProgressProof>(new ResumeProgressProof(binding_, hold_, first_, second_, validity_, nowNs));
}

void ResumeProgress::invalidate()
{
  validity_->store(false); invalid_ = true; first_.reset(); second_.reset();
}
}
