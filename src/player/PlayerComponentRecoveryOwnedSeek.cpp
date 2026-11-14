#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RecoveryIdentity.h"
#include "system/WebClientFaultGuard.h"
#include <QQuickWindow>
#include <cmath>
#include <limits>

void PlayerComponentRecovery::prepareOwnedSeek(std::int64_t targetMs)
{
  seekPrepared_ = false;
#ifdef MPVQT_OWNED_SEEK_API
  const auto now = nowMs();
  if (!bound_ || !item_ || !core_ || targetMs < 0 || policy_.attempted() || !item_->recoveryOwnedSeekAvailable() ||
      !item_->recoveryFrameObservationAvailable() || WebClientFaultGuard::instance().rendererLost()) return;
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto key = runtime_.activeKey();
  const auto entry = runtime_.activeEntry();
  if ((intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused) ||
      key.revision != intent.revision || key.generation != player_->m_mpvGeneration || entry <= 0 ||
      lastObservation_.entry != entry || lastObservation_.observedMs < 0 || now < lastObservation_.observedMs ||
      now - lastObservation_.observedMs >= 2000 || !lastObservation_.seekable || !lastObservation_.video ||
      lastObservation_.seeking || lastObservation_.buffering ||
      lastObservation_.nativePaused != (intent.desired == PlaybackIntent::Desired::Paused) ||
      !player_->m_inPlayback || !player_->m_window || !player_->m_window->isVisible() || !player_->m_window->isExposed() ||
      !item_->isVisible() || !player_->m_windowVisible || player_->m_state == PlayerComponent::State::error ||
      !player_->m_playbackError.isEmpty() || bootstrapEntry_ != static_cast<std::uint64_t>(entry) ||
      !bootstrapEpoch_ || bootstrapEpoch_ != runtime_.eventEpoch() || bootstrapGeneration_ != item_->recoveryRendererGeneration() ||
      !bootstrapDiscontinuity_ || !bootstrapSource_ ||
      (intent.desired != PlaybackIntent::Desired::Paused &&
       (sourceEntry_ != bootstrapEntry_ || sourceEpoch_ != bootstrapEpoch_ || sourceGeneration_ != bootstrapGeneration_ ||
        !sourceFirst_ || lastSource_ <= sourceFirst_ || player_->m_mediaFrameRate < 1.0f)) ||
      !runtime_.snapshot(key, seekCheckpoint_)) return;
  seekWindowLifetime_ = item_->recoveryWindowLifetime(); seekSceneGeneration_ = item_->recoverySceneGraphGeneration();
  if (!seekWindowLifetime_ || !seekSceneGeneration_ || bootstrapWindow_ != seekWindowLifetime_ ||
      bootstrapScene_ != seekSceneGeneration_) return;
  seekBaselineSource_ = bootstrapSource_; seekBaselineDiscontinuity_ = bootstrapDiscontinuity_;
  seekPriorObservation_ = lastObservation_; seekPrepared_ = true;
#else
  (void)targetMs;
#endif
}

int PlayerComponentRecovery::dispatchOwnedSeek(std::int64_t targetMs)
{
#ifdef MPVQT_OWNED_SEEK_API
  if (!seekPrepared_ || !item_ || !core_ || seekCancellationFailed_) return 0;
  seekPrepared_ = false;
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto key = runtime_.activeKey();
  if (targetMs < 0 || key.session != seekCheckpoint_.key.session || key.generation != seekCheckpoint_.key.generation ||
      key.revision != intent.revision || key.revision <= seekCheckpoint_.key.revision ||
      runtime_.activeEntry() != seekCheckpoint_.entry || item_->recoveryRendererGeneration() != bootstrapGeneration_ ||
      item_->recoveryWindowLifetime() != seekWindowLifetime_ || item_->recoverySceneGraphGeneration() != seekSceneGeneration_ ||
      !item_->recoveryOwnedSeekAvailable()) return 0;
  const auto operationId = RecoveryIdentity::take(RecoveryIdentity::Domain::SeekOperation);
  const auto replySequence = RecoveryIdentity::take(RecoveryIdentity::Domain::SeekReply);
  if (!operationId || !replySequence) return -1;
  auto lease = core_->acquireNative();
  if (!lease.get()) return -1;
  AutomaticRecoveryOwnedSeek::Operation op;
  op.key = key; op.operationId = operationId; op.replyId = (std::uint64_t{6} << 61) | replySequence;
  op.baselineEpoch = runtime_.eventEpoch(); op.baselineDiscontinuity = seekBaselineDiscontinuity_; op.baselineSource = seekBaselineSource_;
  op.rendererGeneration = bootstrapGeneration_; op.entry = seekCheckpoint_.entry; op.targetMs = targetMs;
  op.checkpointMs = seekCheckpoint_.positionMs; op.dispatchedMs = nowMs();
  op.desiredPaused = intent.desired == PlaybackIntent::Desired::Paused;
  if (!ownedSeek_.begin(op)) return -1;
  const int result = item_->dispatchRecoveryOwnedSeek(op.replyId, op.operationId, op.entry, targetMs);
  if (result < 0) { ownedSeek_.cancel(); return -1; }
  nativeSeekArmed_ = true; seekCheckpoint_.key = key; return 1;
#else
  (void)targetMs; return 0;
#endif
}

bool PlayerComponentRecovery::cancelOwnedSeek()
{
  ownedSeek_.cancel();
  if (seekCancellationFailed_) return false;
#ifdef MPVQT_OWNED_SEEK_API
  if (nativeSeekArmed_) {
    if (!item_ || item_->cancelRecoveryOwnedSeek() < 0) {
      seekCancellationFailed_ = true;
      if (core_) { auto lease = core_->acquireNative(); if (lease.get()) lease.contain(); }
      return false;
    }
    nativeSeekArmed_ = false;
  }
#endif
  return true;
}

void PlayerComponentRecovery::seekReply(const mpv_event* native, std::int64_t now)
{
  if (native && native->event_id == MPV_EVENT_COMMAND_REPLY) {
    ownedSeek_.reply(native->reply_userdata, native->error, now);
    checkOwnedSeek(now);
  }
}
void PlayerComponentRecovery::seekPosition(const AutomaticRecoveryObservation::Result& result, std::int64_t now)
{
  AutomaticRecoveryOwnedSeek::Position position;
  position.key = runtime_.activeKey(); position.entry = result.entry; position.eventEpoch = runtime_.eventEpoch();
  position.batchSequence = result.batchSequence; position.observedMs = result.observedMs; position.positionMs = result.positionMs;
  position.seekable = result.seekable; position.video = result.video; position.seeking = result.seeking;
  position.buffering = result.buffering; position.nativePaused = result.nativePaused;
  ownedSeek_.position(position, now);
  checkOwnedSeek(now);
}
void PlayerComponentRecovery::seekSubmission(const AutomaticRecoveryOwnedSeek::Submission& value, std::int64_t now)
{ ownedSeek_.submission(value, now); checkOwnedSeek(now); }

void PlayerComponentRecovery::seekQtSubmission(std::uint64_t generation, const QtRecoverySubmissionV1& value,
                                                std::uint64_t operationId)
{
  if (!item_ || !operationId || value.frame.attempt || !AutomaticRecoveryFrameGate::validSubmission(value) ||
      generation != value.frame.renderer_generation || generation != item_->recoveryRendererGeneration() ||
      value.ticket.window_lifetime != seekWindowLifetime_ || value.ticket.scenegraph_generation != seekSceneGeneration_ ||
      item_->recoveryWindowLifetime() != seekWindowLifetime_ || item_->recoverySceneGraphGeneration() != seekSceneGeneration_ ||
      value.frame.media_pts > double(std::numeric_limits<std::int64_t>::max() / 2000)) return;
  AutomaticRecoveryOwnedSeek::Submission submission;
  submission.key = runtime_.activeKey(); submission.operationId = operationId; submission.eventEpoch = runtime_.eventEpoch();
  submission.rendererGeneration = generation; submission.discontinuity = value.frame.discontinuity_id;
  submission.sourceSerial = value.frame.source_frame_serial; submission.renderSerial = value.frame.render_serial;
  if (value.frame.entry_id > std::uint64_t(std::numeric_limits<std::int64_t>::max())) return;
  submission.entry = static_cast<std::int64_t>(value.frame.entry_id);
  submission.ptsMs = static_cast<std::int64_t>(value.frame.media_pts * 1000.0);
  submission.privateSubmissionVerified = true; seekSubmission(submission, nowMs());
}

void PlayerComponentRecovery::checkOwnedSeek(std::int64_t now)
{
  if (ownedSeek_.state() == AutomaticRecoveryOwnedSeek::State::Rejected && nativeSeekArmed_) {
    cancelOwnedSeek(); return;
  }
  if (ownedSeek_.state() != AutomaticRecoveryOwnedSeek::State::Pending) return;
  const auto state = ownedSeek_.poll(runtime_.activeKey(), now);
  if (state == AutomaticRecoveryOwnedSeek::State::ReplyTimeout ||
      state == AutomaticRecoveryOwnedSeek::State::PostconditionTimeout) beginSeekRecovery(state, now);
  else if (state == AutomaticRecoveryOwnedSeek::State::Complete || state == AutomaticRecoveryOwnedSeek::State::Rejected ||
           state == AutomaticRecoveryOwnedSeek::State::Cancelled) cancelOwnedSeek();
}

void PlayerComponentRecovery::beginSeekRecovery(AutomaticRecoveryOwnedSeek::State reason, std::int64_t now)
{
  const auto operation = ownedSeek_.operation();
  seekTimeoutReason_ = reason;
  if (!cancelOwnedSeek() || !item_ || !core_ || !item_->recoveryFrameObservationAvailable() ||
      WebClientFaultGuard::instance().rendererLost() || runtime_.activeEntry() != operation.entry ||
      runtime_.activeKey().session != operation.key.session || runtime_.activeKey().revision != operation.key.revision ||
      player_->m_mpvGeneration != operation.key.generation || item_->recoveryRendererGeneration() != operation.rendererGeneration ||
      item_->recoveryWindowLifetime() != seekWindowLifetime_ || item_->recoverySceneGraphGeneration() != seekSceneGeneration_) return;
  const auto intent = player_->m_playbackIntent.snapshot();
  if (intent.revision != operation.key.revision || intent.desired == PlaybackIntent::Desired::Stopped ||
      intent.desired == PlaybackIntent::Desired::Exiting ||
      (intent.desired == PlaybackIntent::Desired::Paused) != operation.desiredPaused || !player_->m_inPlayback ||
      player_->m_state == PlayerComponent::State::finished || !player_->m_window || !player_->m_window->isVisible() ||
      !player_->m_window->isExposed() || !item_->isVisible() || player_->m_state == PlayerComponent::State::error ||
      !player_->m_playbackError.isEmpty()) return;
  AutomaticRecovery::Sample sample;
  sample.nowMs = now; sample.positionMs = operation.checkpointMs; sample.session = operation.key.session;
  sample.revision = operation.key.revision; sample.generation = operation.key.generation;
  sample.frameSerial = operation.baselineSource; sample.seekable = seekPriorObservation_.seekable;
  sample.video = seekPriorObservation_.video; sample.visible = true; sample.exposed = true;
  sample.desiredPaused = operation.desiredPaused; sample.nativePaused = seekPriorObservation_.nativePaused;
  sample.operationTimedOut = true;
  if (policy_.observe(sample) != AutomaticRecovery::Action::Reload) return;
  saved_ = seekCheckpoint_; saved_.key = operation.key; saved_.positionMs = operation.targetMs; saved_.paused = operation.desiredPaused;
  rendererGeneration_ = item_->recoveryRendererGeneration();
  if (!rendererGeneration_ || !armFrameAttempt()) { clearFrameAttempt(); policy_.fail(); return; }
  awaitingUsability_ = true;
  request_ = RecoveryIdentity::take(RecoveryIdentity::Domain::Usability);
  if (!request_) { failRestore(); return; }
  item_->requestRendererUsability(rendererGeneration_, request_, policy_.deadlineMs());
}
#endif
