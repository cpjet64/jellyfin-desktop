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
                                       std::uint64_t epoch, std::uint64_t previousSource,
                                       std::uint64_t rendererGeneration, std::uint64_t windowLifetime,
                                       std::uint64_t sceneGeneration,
                                       std::int64_t nowMs, std::int64_t deadlineMs)
{
  invalidate();
  if (!key.session || !key.revision || !key.generation || !attempt || !epoch || !rendererGeneration ||
      !windowLifetime || !sceneGeneration || entry <= 0 || targetMs < 0 ||
      nowMs < 0 || deadlineMs <= nowMs)
    return false;
  key_ = key; attempt_ = attempt; epoch_ = epoch; entry_ = entry; target_ = targetMs; discontinuity_ = 0; previousSource_ = previousSource;
  rendererGeneration_ = rendererGeneration; windowLifetime_ = windowLifetime; sceneGeneration_ = sceneGeneration;
  lastMs_ = nowMs; deadline_ = deadlineMs; active_ = true; return true;
}
bool AutomaticRecoveryFrameGate::rendered(Key key, std::uint64_t attempt, std::uint64_t epoch, const RecoveryFrameObservationV1& frame,
                                          std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || frame.size != sizeof(frame) || frame.version != RECOVERY_FRAME_OBSERVATION_VERSION ||
      frame.valid != 1 || frame.reserved || frame.playlist_entry_id != static_cast<std::uint64_t>(entry_) ||
      !frame.discontinuity_id || (discontinuity_ && frame.discontinuity_id != discontinuity_) ||
      frame.discontinuity_id > std::numeric_limits<std::int64_t>::max() ||
      !frame.source_frame_serial || frame.source_frame_serial > std::numeric_limits<std::int64_t>::max() ||
      frame.source_frame_serial <= previousSource_ || !frame.render_serial ||
      frame.render_serial > std::numeric_limits<std::int64_t>::max() || !std::isfinite(frame.media_pts) || frame.media_pts < 0 ||
      frame.media_pts > double(std::numeric_limits<std::int64_t>::max() / 2000)) { invalidate(); return false; }
  const auto ptsMs = static_cast<std::int64_t>(frame.media_pts * 1000.0);
  if (!nearTarget(ptsMs)) { invalidate(); return false; }
  discontinuity_ = frame.discontinuity_id;
  renderSerial_ = frame.render_serial; lastMs_ = now; return true;
}
bool AutomaticRecoveryFrameGate::validSubmission(const QtRecoverySubmissionV1& s)
{
  const auto& t = s.ticket; const auto& f = s.frame;
  return s.size == sizeof(s) && s.version == QT_RECOVERY_SUBMISSION_ABI_VERSION && !s.reserved &&
      t.size == sizeof(t) && t.version == QT_RECOVERY_SWAP_ABI_VERSION && t.window_lifetime && t.scenegraph_generation &&
      t.frame_serial && t.render_thread && t.swapchain && t.render_target && t.surface && t.platform_context &&
      f.size == sizeof(f) && f.version == QT_RECOVERY_SUBMISSION_ABI_VERSION && !f.reserved &&
      f.renderer_generation && f.entry_id && f.discontinuity_id && f.source_frame_serial && f.render_serial &&
      f.actual_render_fbo && std::isfinite(f.media_pts) && f.media_pts >= 0 && s.node_lifetime &&
      s.texture_generation && s.fbo_content_serial && s.display_texture;
}
bool AutomaticRecoveryFrameGate::completeSubmission(Key key, std::uint64_t attempt, std::uint64_t epoch,
                                                     const QtRecoverySubmissionV1& s, std::int64_t now)
{
  if (!matches(key, attempt, epoch, now) || !validSubmission(s) || !attempt || s.frame.attempt != attempt ||
      s.frame.renderer_generation != rendererGeneration_ || s.ticket.window_lifetime != windowLifetime_ ||
      s.ticket.scenegraph_generation != sceneGeneration_) { invalidate(); return false; }
  RecoveryFrameObservationV1 frame{};
  frame.size = sizeof(frame); frame.version = RECOVERY_FRAME_OBSERVATION_VERSION; frame.valid = 1;
  frame.playlist_entry_id = s.frame.entry_id; frame.discontinuity_id = s.frame.discontinuity_id;
  frame.source_frame_serial = s.frame.source_frame_serial; frame.render_serial = s.frame.render_serial;
  frame.media_pts = s.frame.media_pts;
  if (!rendered(key, attempt, epoch, frame, now)) return false;
  completed_ = true; return true;
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
{ return matches(key, attempt, epoch, now) && positioned_ && now - positionMsAt_ < 2000 && completed_; }
void AutomaticRecoveryFrameGate::invalidate()
{
  active_ = completed_ = positioned_ = false; key_ = {}; attempt_ = renderSerial_ = 0;
}
