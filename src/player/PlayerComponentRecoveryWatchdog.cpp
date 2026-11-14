#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RendererOwners.h"
#include "system/WebClientFaultGuard.h"
#include <QQuickWindow>
#include <cmath>
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
  if (native && native->event_id == MPV_EVENT_START_FILE && native->data)
    lastStartedEntry_ = static_cast<const mpv_event_start_file*>(native->data)->playlist_entry_id;
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
    policy_.suspendBaseline();
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
      intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) return;
  if (observationPending_ && (observation_.failed() || now - observationStarted_ >= 2000)) {
    observation_.invalidate(); observationPending_ = false;
    policy_.suspendBaseline();
  }
  if (!observationPending_) { beginObservation(key, entry, now); if (!observationPending_) policy_.suspendBaseline(); return; }
  if (!observation_.ready()) return;
  AutomaticRecoveryObservation::Result result;
  observationPending_ = false;
  if (!observation_.take(key, runtime_.eventEpoch(), now, result) || result.entry != entry ||
      !runtime_.recordPosition(key, entry, result.positionMs, now)) { policy_.suspendBaseline(); return; }
  AutomaticRecoverySession::Snapshot snapshot;
  if (!runtime_.snapshot(key, snapshot)) return;
  AutomaticRecovery::Sample sample;
  sample.nowMs = now; sample.positionMs = result.positionMs; sample.session = key.session;
  sample.revision = key.revision; sample.generation = key.generation; sample.frameSerial = lastSource_;
  sample.seekable = true; sample.video = true; sample.nativePaused = result.nativePaused;
  sample.desiredPaused = intent.desired == PlaybackIntent::Desired::Paused;
  sample.loading = !player_->m_inPlayback; sample.seeking = false; sample.buffering = player_->m_state == PlayerComponent::State::buffering;
  sample.visible = player_->m_window && player_->m_window->isVisible() && player_->m_windowVisible;
  sample.exposed = player_->m_window && player_->m_window->isExposed();
  sample.eof = player_->m_state == PlayerComponent::State::finished;
  sample.movingVideo = sourceEntry_ == static_cast<std::uint64_t>(entry) && sourceEpoch_ == runtime_.eventEpoch() &&
      sourceGeneration_ == item_->recoveryRendererGeneration() &&
      sourceFirst_ && lastSource_ > sourceFirst_ && player_->m_mediaFrameRate >= 1.0f;
  sample.outstanding = sample.movingVideo;
  std::uint64_t expired = 0;
  sample.operationTimedOut = runtime_.takeTimeout(key, now, expired);
  sample.rendererFault = WebClientFaultGuard::instance().rendererLost();
  const auto action = policy_.observe(sample);
  if (action == AutomaticRecovery::Action::Blocked || action == AutomaticRecovery::Action::Cancel) {
    awaitingUsability_ = false; frameGate_.invalidate(); return;
  }
  if (action != AutomaticRecovery::Action::Reload) return;
  if (request_ == std::numeric_limits<std::uint64_t>::max()) { policy_.fail(); return; }
  saved_ = snapshot; rendererGeneration_ = item_->recoveryRendererGeneration();
  if (!rendererGeneration_) { policy_.fail(); return; }
  awaitingUsability_ = true;
  item_->requestRendererUsability(rendererGeneration_, ++request_, policy_.deadlineMs());
}
#endif
