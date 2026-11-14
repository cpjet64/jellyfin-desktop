#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "MpvVideoItem.h"

bool PlayerComponentRecovery::armFrameAttempt()
{
#ifdef MPVQT_RECOVERY_FRAME_API
  if (!item_ || !rendererGeneration_) return false;
  windowLifetime_ = item_->recoveryWindowLifetime();
  sceneGeneration_ = item_->recoverySceneGraphGeneration();
  attempt_ = runtime_.reserveSequence();
  if (!windowLifetime_ || !sceneGeneration_ || !attempt_) return false;
  previousSource_ = lastSource_; submissionPending_ = false;
#ifdef MPVQT_OWNED_SEEK_API
  item_->restrictRecoveryBootstrapDeadline(policy_.deadlineMs());
#endif
  item_->setRecoveryFrameAttempt(rendererGeneration_, attempt_, policy_.deadlineMs());
  return true;
#else
  return false;
#endif
}

void PlayerComponentRecovery::clearFrameAttempt()
{
#ifdef MPVQT_RECOVERY_FRAME_API
  if (item_ && rendererGeneration_ && attempt_) item_->clearRecoveryFrameAttempt(rendererGeneration_, attempt_);
#endif
  submissionPending_ = false; pendingSubmission_ = {}; pendingSubmissionEpoch_ = 0;
  windowLifetime_ = sceneGeneration_ = attempt_ = 0;
}

void PlayerComponentRecovery::submission(std::uint64_t generation, const QtRecoverySubmissionV1& value)
{
  if (!AutomaticRecoveryFrameGate::validSubmission(value) || generation != value.frame.renderer_generation) return;
  if (!value.frame.attempt) {
    pendingBootstrap_ = value; pendingBootstrapEpoch_ = runtime_.eventEpoch(); bootstrapPending_ = true;
    acceptBootstrapSubmission();
    return;
  }
  if (!restoring_ || value.frame.attempt != attempt_ || generation != rendererGeneration_) return;
  const auto now = nowMs();
  if (!validRestore(now) || value.ticket.window_lifetime != windowLifetime_ ||
      value.ticket.scenegraph_generation != sceneGeneration_) { failRestore(); return; }
  if (!frameGateActive_) {
    pendingSubmission_ = value; pendingSubmissionEpoch_ = runtime_.eventEpoch(); submissionPending_ = true; return;
  }
  if (frameGate_.completed()) return;
  if (runtime_.eventEpoch() != frameEpoch_ ||
      !frameGate_.completeSubmission(saved_.key, attempt_, frameEpoch_, value, now)) { failRestore(); return; }
  verifiedSource_ = value.frame.source_frame_serial;
  maybeResume(now);
}

void PlayerComponentRecovery::acceptBootstrapSubmission()
{
#ifdef MPVQT_RECOVERY_FRAME_API
  if (!bootstrapPending_ || !item_) return;
  if (!pendingBootstrapEpoch_ || pendingBootstrapEpoch_ != runtime_.eventEpoch()) { bootstrapPending_ = false; return; }
  const auto entry = runtime_.activeEntry();
  if (entry <= 0) return;
  const auto& value = pendingBootstrap_;
  if (value.frame.entry_id != static_cast<std::uint64_t>(entry) ||
      value.frame.renderer_generation != item_->recoveryRendererGeneration() ||
      value.ticket.window_lifetime != item_->recoveryWindowLifetime() ||
      value.ticket.scenegraph_generation != item_->recoverySceneGraphGeneration()) { bootstrapPending_ = false; return; }
  RecoveryFrameObservationV1 frame{};
  frame.size = sizeof(frame); frame.version = RECOVERY_FRAME_OBSERVATION_VERSION; frame.valid = 1;
  frame.playlist_entry_id = value.frame.entry_id; frame.discontinuity_id = value.frame.discontinuity_id;
  frame.source_frame_serial = value.frame.source_frame_serial; frame.render_serial = value.frame.render_serial;
  frame.media_pts = value.frame.media_pts; recordSourceFrame(value.frame.renderer_generation, frame);
  bootstrapEntry_ = value.frame.entry_id; bootstrapSource_ = value.frame.source_frame_serial;
  bootstrapDiscontinuity_ = value.frame.discontinuity_id; bootstrapGeneration_ = value.frame.renderer_generation;
  bootstrapEpoch_ = pendingBootstrapEpoch_; bootstrapWindow_ = value.ticket.window_lifetime;
  bootstrapScene_ = value.ticket.scenegraph_generation; bootstrapPending_ = false;
#endif
}
#endif
