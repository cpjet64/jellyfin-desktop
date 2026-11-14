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
    sourceFirst_ = frame.source_frame_serial; lastSource_ = frame.source_frame_serial;
    return;
  }
  if (frame.source_frame_serial > lastSource_) lastSource_ = frame.source_frame_serial;
}

void PlayerComponentRecovery::event(const mpv_event* native, std::int64_t now)
{
  if (native && native->event_id == MPV_EVENT_START_FILE && native->data) {
    lastStartedEntry_ = static_cast<const mpv_event_start_file*>(native->data)->playlist_entry_id;
    ++startSerial_;
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
      static_cast<const mpv_event_end_file*>(native->data)->playlist_entry_id == replacedEntry_) replacedEntry_ = 0;
  if (restoring_) {
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
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto active = runtime_.activeKey();
  const AutomaticRecoverySession::Key current{active.session, intent.revision, player_->m_mpvGeneration};
  runtime_.observe(native, current, now);
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
  const auto now = nowMs();
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
  if (awaitingUsability_ && now >= policy_.deadlineMs()) {
    awaitingUsability_ = false; policy_.fail(); frameGate_.invalidate(); return;
  }
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto key = runtime_.activeKey();
  const auto entry = runtime_.activeEntry();
  if (!key.session || key.revision != intent.revision || key.generation != player_->m_mpvGeneration || entry <= 0 ||
      intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) { readStall_.reset(); return; }
  if (observationPending_ && (observation_.failed() || now - observationStarted_ >= 2000)) {
    observation_.invalidate(); observationPending_ = false;
    policy_.suspendBaseline(); readStall_.reset();
  }
  if (!observationPending_) { beginObservation(key, entry, now); if (!observationPending_) { policy_.suspendBaseline(); readStall_.reset(); } return; }
  if (!observation_.ready()) return;
  AutomaticRecoveryObservation::Result result;
  observationPending_ = false;
  if (!observation_.take(key, runtime_.eventEpoch(), now, result) || result.entry != entry ||
      !result.seekable || result.seeking || !result.video ||
      !runtime_.recordPosition(key, entry, result.positionMs, now)) { policy_.suspendBaseline(); readStall_.reset(); return; }
  watchdogObservation(key, result, now);
}
#endif
