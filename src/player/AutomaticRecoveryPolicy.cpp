#include "AutomaticRecoveryPolicy.h"
#include <limits>

namespace AutomaticRecovery {
bool Policy::active() const
{
  return phase_ == Phase::Reloading || phase_ == Phase::Retiring ||
      phase_ == Phase::Restoring || phase_ == Phase::WaitingFrame;
}
bool Policy::inTime(std::int64_t now) const { return now >= 0 && now < deadline_; }
void Policy::resetBaseline(const Sample& sample)
{
  baseline_ = true;
  baselineTime_ = sample.nowMs;
  baselinePosition_ = sample.positionMs;
  lastPosition_ = sample.positionMs;
  lastAdvanceTime_ = sample.nowMs;
  baselineFrame_ = sample.frameSerial;
  baselineRevision_ = sample.revision;
  baselineGeneration_ = sample.generation;
  baselineOutstanding_ = sample.outstanding;
}
Action Policy::observe(const Sample& sample)
{
  if (!sample.session || sample.nowMs < 0) { if (active()) fail(); return Action::Cancel; }
  if (sample.session != session_) {
    const bool cancel = active();
    session_ = sample.session; attempted_ = false; baseline_ = false; phase_ = Phase::Idle;
    if (cancel) return Action::Cancel;
  }
  if (active()) {
    if (!inTime(sample.nowMs) || sample.revision != revision_ ||
        (sample.generation != generation_ && sample.generation != replacement_)) {
      fail(); return Action::Cancel;
    }
    return Action::None;
  }
  const bool eligible = sample.seekable && sample.video && !sample.loading && !sample.seeking &&
      !sample.buffering && sample.visible && sample.exposed && !sample.eof;
  if (attempted_ || !eligible || !sample.generation) { baseline_ = false; return Action::None; }
  const bool reset = !baseline_ || sample.paused || !sample.outstanding || sample.positionMs < 0 ||
      sample.nowMs < baselineTime_ || sample.positionMs < baselinePosition_ || sample.frameSerial != baselineFrame_ ||
      sample.revision != baselineRevision_ || sample.generation != baselineGeneration_ ||
      sample.outstanding != baselineOutstanding_;
  if (reset) resetBaseline(sample);
  if (sample.positionMs > lastPosition_) { lastPosition_ = sample.positionMs; lastAdvanceTime_ = sample.nowMs; }
  // The 1.5 second freshness window is a policy choice requiring runtime validation.
  const bool stalled = !sample.paused && sample.outstanding && sample.positionMs > baselinePosition_ &&
      sample.nowMs - baselineTime_ >= 5000 && sample.nowMs - lastAdvanceTime_ <= 1500;
  if (!sample.operationTimedOut && !sample.rendererFault && !stalled) return Action::None;
  if (sample.nowMs > std::numeric_limits<std::int64_t>::max() - 15000) { fail(); return Action::Blocked; }
  attempted_ = true; revision_ = sample.revision; generation_ = sample.generation;
  replacement_ = 0; deadline_ = sample.nowMs + 15000; wasPaused_ = sample.paused;
  phase_ = sample.operationTimedOut && !sample.rendererFault && !stalled ? Phase::Reloading : Phase::Retiring;
  return phase_ == Phase::Reloading ? Action::Reload : Action::Retire;
}
bool Policy::retired(std::uint64_t original, std::uint64_t replacement, std::int64_t now)
{
  if (phase_ != Phase::Retiring || !inTime(now) || original != generation_ || !replacement || replacement == original) {
    fail(); return false;
  }
  replacement_ = replacement; phase_ = Phase::Restoring; return true;
}
bool Policy::restoreStarted(std::int64_t now)
{
  if ((phase_ != Phase::Reloading && phase_ != Phase::Restoring) || !inTime(now)) { fail(); return false; }
  if (phase_ == Phase::Reloading) replacement_ = generation_;
  phase_ = Phase::WaitingFrame; return true;
}
bool Policy::frameObserved(std::uint64_t generation, std::int64_t now)
{
  if (phase_ != Phase::WaitingFrame || !inTime(now) || generation != replacement_) { fail(); return false; }
  phase_ = Phase::Complete; return true;
}
bool Policy::mayUnpause(const Sample& sample) const
{
  return phase_ == Phase::Complete && !wasPaused_ && inTime(sample.nowMs) && sample.session == session_ &&
      sample.revision == revision_ && sample.generation == replacement_ && !sample.paused;
}
void Policy::fail() { phase_ = Phase::Failed; baseline_ = false; }
}
