#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RendererOwners.h"
#include "system/WebClientFaultGuard.h"
#include <QQuickWindow>
#include <cmath>
#include <cstring>
#include <limits>

void PlayerComponentRecovery::recordSourceFrame(std::uint64_t generation, const RecoveryFrameObservationV1& frame)
{
  const auto entry = runtime_.activeEntry();
  if (frame.size != sizeof(RecoveryFrameObservationV1) || frame.version != RECOVERY_FRAME_OBSERVATION_VERSION ||
      frame.valid != 1 || frame.reserved || !frame.discontinuity_id || !frame.source_frame_serial || !frame.render_serial ||
      !std::isfinite(frame.media_pts) || frame.media_pts < 0 || entry <= 0 || !runtime_.eventEpoch() ||
      frame.playlist_entry_id != static_cast<std::uint64_t>(entry) || !item_ ||
      generation != item_->recoveryRendererGeneration()) return;
  if (sourceEntry_ != frame.playlist_entry_id || sourceGeneration_ != generation || sourceEpoch_ != runtime_.eventEpoch()) {
    sourceEntry_ = frame.playlist_entry_id; sourceGeneration_ = generation;
    sourceEpoch_ = runtime_.eventEpoch();
    sourceDiscontinuity_ = frame.discontinuity_id;
    sourceFirst_ = frame.source_frame_serial; lastSource_ = frame.source_frame_serial;
    return;
  }
  if (frame.discontinuity_id != sourceDiscontinuity_) {
    sourceDiscontinuity_ = frame.discontinuity_id; sourceFirst_ = frame.source_frame_serial;
  }
  if (frame.source_frame_serial > lastSource_) lastSource_ = frame.source_frame_serial;
}

void PlayerComponentRecovery::event(const mpv_event* native, std::int64_t now, bool ordinaryReplacementEnd)
{
  if (native && native->event_id == MPV_EVENT_START_FILE && native->data) {
    lastStartedEntry_ = static_cast<const mpv_event_start_file*>(native->data)->playlist_entry_id;
    normalCurrentEntry_ = lastStartedEntry_;
    if (startSerial_ == std::numeric_limits<std::uint64_t>::max()) {
      startSerialExhausted_ = true; ordinaryLedger_.failClosed(); ordinarySelection_.revoke(); ordinaryReplacement_.revoke();
    } else if (!startSerialExhausted_) ++startSerial_;
    ordinaryReplacement_.started(startSerial_);
  }
  bool hookEvent = false;
  hookTokenValid_ = false;
  if (native && native->event_id == MPV_EVENT_CLIENT_MESSAGE && native->data) {
    const auto* message = static_cast<const mpv_event_client_message*>(native->data);
    hookEvent = message->num_args >= 3 && message->args && message->args[0] && message->args[1] && message->args[2] &&
        !std::strcmp(message->args[0], "hook_run") && !std::strcmp(message->args[1], "2");
    if (hookEvent) hookToken_ = QString::fromUtf8(message->args[2]);
  }
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
  if (native && native->event_id == MPV_EVENT_HOOK && native->data) {
    const auto* hook = static_cast<const mpv_event_hook*>(native->data);
    hookEvent = hook->name && !std::strcmp(hook->name, "on_preloaded"); hookToken_ = QString::number(hook->id);
  }
#endif
  if (hookEvent) {
    hookTokenValid_ = true;
    hookStart_ = startSerial_; hookEntry_ = lastStartedEntry_; hookBinding_ = player_->m_mpvGeneration;
    hookRevision_ = player_->m_playbackIntent.snapshot().revision; hookAttempt_ = restoring_ ? attempt_ : 0;
  }
  if (native && native->event_id == MPV_EVENT_END_FILE && native->data &&
      static_cast<const mpv_event_end_file*>(native->data)->playlist_entry_id == normalCurrentEntry_) normalCurrentEntry_ = 0;
  if (restoring_) {
    if (native && native->event_id == MPV_EVENT_COMMAND_REPLY)
      ordinaryLedger_.reply(native->reply_userdata, native->error >= 0);
    if (native && (native->event_id == MPV_EVENT_QUEUE_OVERFLOW || native->event_id == MPV_EVENT_SHUTDOWN)) {
      ordinaryReplacement_.revoke(); ordinaryLedger_.failClosed(); ordinarySelection_.revoke(); ordinaryAfterId_ = 0;
    }
    runtime_.internalEvent(native);
    restoreEvent(native, now);
    if (!restoring_ || !observationPending_ || !core_) return;
    const auto dispatch = [this](std::uint64_t id, const char* name, mpv_format format) {
      auto lease = core_->acquireNative();
      return lease.get() ? mpv_get_property_async(lease.get(), id, name, format) : -1;
    };
    observation_.observe(native, saved_.key, runtime_.eventEpoch(), now, dispatch);
    return;
  }
  ordinaryEvent(native, now, ordinaryReplacementEnd);
  const auto intent = player_->m_playbackIntent.snapshot();
  seekReply(native, now);
  const auto active = runtime_.activeKey();
  const AutomaticRecoverySession::Key current{active.session, intent.revision, player_->m_mpvGeneration};
  runtime_.observe(native, current, now);
  acceptBootstrapSubmission();
  applyDeferredTracks();
  if (!observationPending_ || !core_) return;
  const auto dispatch = [this](std::uint64_t id, const char* name, mpv_format format) {
    auto lease = core_->acquireNative();
    return lease.get() ? mpv_get_property_async(lease.get(), id, name, format) : -1;
  };
  observation_.observe(native, active, runtime_.eventEpoch(), now, dispatch);
}

void PlayerComponentRecovery::tick()
{
  const auto now = clockNow();
  ordinaryReplacement_.expire(now);
  if (ordinaryLoad_.pending && now >= ordinaryLoad_.deadlineMs) flushOrdinaryLoad(false);
  checkOwnedSeek(now);
  if (replacementTransition_ && now >= transitionDeadline_) {
    replacementTransition_ = false; player_->updatePlaybackState();
  }
  if ((restoring_ || awaitingUsability_) && now >= policy_.deadlineMs()) { failRestore(); return; }
  if (!bound_ || !item_ || !core_ || !item_->recoveryFrameObservationAvailable()) {
    policy_.suspendBaseline(); readStall_.reset();
    if (restoring_ || awaitingUsability_) failRestore();
    return;
  }
  if (restoring_) { recoveryTick(now); return; }
  if (awaitingUsability_) return;
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto key = runtime_.activeKey();
  const auto entry = runtime_.activeEntry();
  if (!key.session || key.revision != intent.revision || key.generation != player_->m_mpvGeneration || entry <= 0 ||
      intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) { readStall_.reset(); return; }
  if (intent.desired == PlaybackIntent::Desired::Paused &&
      (bootstrapEntry_ != static_cast<std::uint64_t>(entry) || bootstrapEpoch_ != runtime_.eventEpoch())) item_->update();
  if (observationPending_ && (observation_.failed() || now - observationStarted_ >= 2000)) {
    observation_.invalidate(); observationPending_ = false;
    policy_.suspendBaseline(); readStall_.reset();
  }
  if (!observationPending_) { beginObservation(key, entry, now); if (!observationPending_) { policy_.suspendBaseline(); readStall_.reset(); } return; }
  if (!observation_.ready()) return;
  AutomaticRecoveryObservation::Result result;
  observationPending_ = false;
  if (!observation_.take(key, runtime_.eventEpoch(), now, result) || result.entry != entry) {
    policy_.suspendBaseline(); readStall_.reset(); return;
  }
  seekPosition(result, now);
  if (!result.seekable || result.seeking || !result.video ||
      !runtime_.recordPosition(key, entry, result.positionMs, now)) { policy_.suspendBaseline(); readStall_.reset(); return; }
  lastObservation_ = result;
  if (ownedSeek_.state() == AutomaticRecoveryOwnedSeek::State::Pending) return;
  watchdogObservation(key, result, now);
}
#endif
