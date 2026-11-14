#include "AutomaticRecoveryFrameGate.h"
#include <cmath>
#include <limits>

bool AutomaticRecoveryFrameGate::matches(Key key, std::uint64_t attempt, std::uint64_t epoch, std::int64_t now) const
{
  return active_ && key.session == key_.session && key.revision == key_.revision &&
      key.generation == key_.generation && attempt == attempt_ && epoch && epoch == epoch_ && now >= lastMs_ && now < deadline_;
}
bool AutomaticRecoveryFrameGate::nearTarget(std::int64_t value) const
{
  if (value < 0) return false;
  return value >= target_ ? value - target_ <= PositionToleranceMs : target_ - value <= PositionToleranceMs;
}
bool AutomaticRecoveryFrameGate::begin(Key key, std::uint64_t attempt, std::int64_t entry, std::int64_t targetMs,
                                       std::uint64_t epoch, std::uint64_t discontinuity, std::uint64_t previousSource,
                                       std::int64_t nowMs, std::int64_t deadlineMs)
{
  invalidate();
  if (!key.session || !key.revision || !key.generation || !attempt || !epoch || !discontinuity || entry <= 0 || targetMs < 0 ||
      nowMs < 0 || deadlineMs <= nowMs)
    return false;
  key_ = key; attempt_ = attempt; epoch_ = epoch; entry_ = entry; target_ = targetMs; discontinuity_ = discontinuity; previousSource_ = previousSource;
  lastMs_ = nowMs; deadline_ = deadlineMs; active_ = true; return true;
}
bool AutomaticRecoveryFrameGate::rendered(Key key, std::uint64_t attempt, std::uint64_t epoch, const RecoveryFrameObservationV1& frame,
                                          std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || frame.size != sizeof(frame) || frame.version != RECOVERY_FRAME_OBSERVATION_VERSION ||
      frame.valid != 1 || frame.reserved || frame.playlist_entry_id != static_cast<std::uint64_t>(entry_) ||
      frame.discontinuity_id != discontinuity_ || frame.discontinuity_id > std::numeric_limits<std::int64_t>::max() ||
      !frame.source_frame_serial || frame.source_frame_serial > std::numeric_limits<std::int64_t>::max() ||
      frame.source_frame_serial <= previousSource_ || !frame.render_serial ||
      frame.render_serial > std::numeric_limits<std::int64_t>::max() || !std::isfinite(frame.media_pts) || frame.media_pts < 0 ||
      frame.media_pts > double(std::numeric_limits<std::int64_t>::max() / 2000)) { invalidate(); return false; }
  const auto ptsMs = static_cast<std::int64_t>(frame.media_pts * 1000.0);
  if (!nearTarget(ptsMs)) { invalidate(); return false; }
  renderSerial_ = frame.render_serial; submitted_ = swapped_ = false; lastMs_ = now; return true;
}
bool AutomaticRecoveryFrameGate::submitted(Key key, std::uint64_t attempt, std::uint64_t epoch, std::uint64_t serial, std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || !renderSerial_ || serial != renderSerial_ || submitted_) { invalidate(); return false; }
  submitted_ = true; lastMs_ = now; return true;
}
bool AutomaticRecoveryFrameGate::swapped(Key key, std::uint64_t attempt, std::uint64_t epoch, std::uint64_t serial, std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || !submitted_ || serial != renderSerial_ || swapped_) { invalidate(); return false; }
  swapped_ = true; lastMs_ = now; return true;
}
bool AutomaticRecoveryFrameGate::position(Key key, std::uint64_t attempt, std::int64_t entry,
                                          std::int64_t positionMs, bool nativePaused, std::uint64_t epoch, std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || entry != entry_ || !nativePaused || !nearTarget(positionMs)) {
    invalidate(); return false;
  }
  positioned_ = true; positionMsAt_ = lastMs_ = now; return true;
}
bool AutomaticRecoveryFrameGate::ready(Key key, std::uint64_t attempt, std::uint64_t epoch, std::int64_t now) const
{ return matches(key, attempt, epoch, now) && positioned_ && now - positionMsAt_ < 2000 && submitted_ && swapped_; }
void AutomaticRecoveryFrameGate::invalidate()
{
  active_ = submitted_ = swapped_ = positioned_ = false; key_ = {}; attempt_ = renderSerial_ = 0;
}
