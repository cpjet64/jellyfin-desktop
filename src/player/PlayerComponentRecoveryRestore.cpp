#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "AutomaticRecoveryNative.h"
#include "AutomaticRecoveryReload.h"
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RendererOwners.h"

bool PlayerComponentRecovery::validRestore(std::int64_t now) const
{
  return restoring_ && now >= 0 && now < policy_.deadlineMs() && sameIntent() && item_ &&
      item_->recoveryFrameObservationAvailable() && rendererGeneration_ &&
      item_->recoveryRendererGeneration() == rendererGeneration_;
}
bool PlayerComponentRecovery::recoveryPreloadAllowed() const
{
  return validRestore(nowMs()) && loadDispatched_ && recoveryLoad_.state() == AutomaticRecoveryLoad::State::Accepted &&
      recoveryLoad_.entry() == lastStartedEntry_;
}
bool PlayerComponentRecovery::prepareRecoveryPreload()
{
  if (!recoveryPreloadAllowed()) return false;
  player_->m_currentAudioStream = saved_.audio;
  player_->m_currentSubtitleStream = saved_.subtitle;
  return true;
}

void PlayerComponentRecovery::failRestore()
{
  if (recoveryLoad_.startedEntry() > 0) cancelledRecoveryEntry_ = recoveryLoad_.startedEntry();
  restoring_ = awaitingUsability_ = resumePending_ = frameGateActive_ = false;
  observationPending_ = false; observation_.invalidate(); frameGate_.invalidate();
  recoveryLoad_.invalidate(); policy_.fail();
}

bool PlayerComponentRecovery::preparePreload(const QString& hookToken)
{
  if (!hookTokenValid_ || hookToken != hookToken_ || !hookEntry_ || hookEntry_ == cancelledRecoveryEntry_ || hookStart_ != startSerial_ ||
      hookBinding_ != player_->m_mpvGeneration || hookRevision_ != player_->m_playbackIntent.snapshot().revision) return false;
  if (hookAttempt_) {
    if (hookAttempt_ == attempt_ && prepareRecoveryPreload()) return true;
  } else {
    const AutomaticRecoverySession::Key current{0, hookRevision_, hookBinding_};
    if (runtime_.selections(hookEntry_, current, player_->m_currentAudioStream, player_->m_currentSubtitleStream)) return true;
  }
  deferredEntry_ = hookEntry_; deferredStart_ = hookStart_; deferredRevision_ = hookRevision_;
  deferredBinding_ = hookBinding_; deferredAttempt_ = hookAttempt_;
  return false;
}

void PlayerComponentRecovery::finishPreload()
{
  if (hookAttempt_ && hookAttempt_ == attempt_) recoveryTracksApplied_ = true;
  deferredEntry_ = 0;
}

void PlayerComponentRecovery::applyDeferredTracks()
{
  if (!deferredEntry_ || deferredEntry_ != lastStartedEntry_ || deferredStart_ != startSerial_ ||
      deferredBinding_ != player_->m_mpvGeneration || deferredRevision_ != player_->m_playbackIntent.snapshot().revision) return;
  if (deferredAttempt_) {
    if (!restoring_ || deferredAttempt_ != attempt_ || !recoveryLoaded_ || !prepareRecoveryPreload()) return;
  } else {
    const AutomaticRecoverySession::Key current{0, deferredRevision_, deferredBinding_};
    if (runtime_.activeEntry() != deferredEntry_ ||
        !runtime_.selections(deferredEntry_, current, player_->m_currentAudioStream, player_->m_currentSubtitleStream)) return;
  }
  player_->reselectStream(player_->m_currentSubtitleStream, PlayerComponent::MediaType::Subtitle);
  player_->reselectStream(player_->m_currentAudioStream, PlayerComponent::MediaType::Audio);
  if (deferredAttempt_) recoveryTracksApplied_ = true;
  deferredEntry_ = 0;
}

void PlayerComponentRecovery::usability(std::uint64_t generation, std::uint64_t request, bool usable)
{
  if (!awaitingUsability_ || generation != rendererGeneration_ || request != request_) return;
  awaitingUsability_ = false;
  restoring_ = true;
  const auto now = nowMs();
  if (!usable || !validRestore(now)) { failRestore(); return; }
  const auto sequence = runtime_.reserveSequence();
  if (!sequence || !recoveryLoad_.begin({saved_.key.session, saved_.key.revision, saved_.key.generation},
                                        sequence, policy_.deadlineMs())) { failRestore(); return; }
  attempt_ = sequence; previousSource_ = lastSource_; pauseId_ = (std::uint64_t{4} << 61) | sequence;
  auto lease = core_->acquireNative();
  int paused = 1;
  if (!lease.get() || mpv_set_property_async(lease.get(), pauseId_, "pause", MPV_FORMAT_FLAG, &paused) < 0)
    failRestore();
}

void PlayerComponentRecovery::dispatchReload(std::int64_t now)
{
  if (!validRestore(now) || loadDispatched_) { failRestore(); return; }
  auto lease = core_->acquireNative();
  if (!lease.get() || !AutomaticRecoveryReload::submit(saved_, recoveryLoad_.userdata(),
      [&lease](std::uint64_t id, mpv_node* node) { return mpv_command_node_async(lease.get(), id, node); })) {
    failRestore(); return;
  }
  loadDispatched_ = true;
}

void PlayerComponentRecovery::restoreEvent(const mpv_event* native, std::int64_t now)
{
  if (!native || !validRestore(now)) { failRestore(); return; }
  if (pauseId_ && native->event_id == MPV_EVENT_SET_PROPERTY_REPLY && native->reply_userdata == pauseId_) {
    pauseId_ = 0;
    if (native->error < 0) failRestore(); else dispatchReload(now);
    return;
  }
  if (native->event_id == MPV_EVENT_SET_PROPERTY_REPLY && native->reply_userdata == unpauseId_ && resumePending_) {
    resumePending_ = false; unpauseId_ = 0;
    if (native->error < 0) failRestore(); else { completed_ = true; restoring_ = false; }
    return;
  }
  if (!loadDispatched_) return;
  if (native->event_id == MPV_EVENT_COMMAND_REPLY && native->reply_userdata == recoveryLoad_.userdata()) {
    const auto entry = AutomaticRecoveryNative::loadEntry(native, recoveryLoad_.userdata());
    recoveryLoad_.reply(native->reply_userdata, entry > 0, entry, now);
  } else if (native->event_id == MPV_EVENT_START_FILE) {
    const auto* start = static_cast<const mpv_event_start_file*>(native->data);
    recoveryLoad_.started(start ? start->playlist_entry_id : 0, now);
  } else if (native->event_id == MPV_EVENT_FILE_LOADED) {
    if (!recoveryLoad_.startedEntry() || recoveryLoaded_) { failRestore(); return; }
    recoveryLoaded_ = true;
  } else if (native->event_id == MPV_EVENT_PLAYBACK_RESTART) {
    if (!recoveryLoad_.startedEntry() || recoveryRestarted_) { failRestore(); return; }
    recoveryRestarted_ = true;
  } else if (native->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(native->data);
    if (!end || end->playlist_entry_id != saved_.entry) recoveryLoad_.ended(end ? end->playlist_entry_id : 0);
  } else if (native->event_id == MPV_EVENT_QUEUE_OVERFLOW || native->event_id == MPV_EVENT_SHUTDOWN) {
    failRestore(); return;
  }
  const auto state = recoveryLoad_.poll({saved_.key.session, saved_.key.revision, saved_.key.generation}, now);
  if (state == AutomaticRecoveryLoad::State::Failed || state == AutomaticRecoveryLoad::State::Expired) {
    failRestore(); return;
  }
  if (state == AutomaticRecoveryLoad::State::Accepted && recoveryLoaded_ && recoveryRestarted_ && !frameGateActive_) beginFrameGate(now);
}

void PlayerComponentRecovery::beginFrameGate(std::int64_t now)
{
  if (!validRestore(now)) { failRestore(); return; }
  recoveryEntry_ = recoveryLoad_.entry(); frameEpoch_ = runtime_.eventEpoch();
  if (!policy_.restoreStarted(attempt_, previousSource_, now) ||
      !frameGate_.begin(saved_.key, attempt_, recoveryEntry_, saved_.positionMs, frameEpoch_, previousSource_, now,
                        policy_.deadlineMs())) { failRestore(); return; }
  frameGateActive_ = true; observation_.invalidate(); observationPending_ = false;
  applyDeferredTracks();
  beginObservation(saved_.key, recoveryEntry_, now);
}

void PlayerComponentRecovery::recoveryTick(std::int64_t now)
{
  if (!validRestore(now) || (frameGateActive_ && runtime_.eventEpoch() != frameEpoch_)) { failRestore(); return; }
  if (!frameGateActive_ || resumePending_) return;
  if (observationPending_ && now - observationStarted_ >= 2000) {
    observationPending_ = false; observation_.invalidate();
  }
  if (observationPending_ && observation_.ready()) {
    observationPending_ = false;
    AutomaticRecoveryObservation::Result result;
    if (!observation_.take(saved_.key, frameEpoch_, now, result) || !result.seekable || result.seeking || result.buffering || !result.video ||
        !frameGate_.position(saved_.key, attempt_, result.entry, result.positionMs, result.nativePaused, frameEpoch_, now)) {
      failRestore(); return;
    }
    observationResult_ = result; positionReady_ = true; maybeResume(now);
  }
  if (restoring_ && !resumePending_ && !observationPending_) beginObservation(saved_.key, recoveryEntry_, now);
}

void PlayerComponentRecovery::rendered(std::uint64_t generation, const RecoveryFrameObservationV1& frame)
{
  recordSourceFrame(generation, frame);
  if (!restoring_ || !frameGateActive_) return;
  const auto now = nowMs();
  if (!validRestore(now) || generation != rendererGeneration_ || runtime_.eventEpoch() != frameEpoch_ ||
      !frameGate_.rendered(saved_.key, attempt_, frameEpoch_, frame, now)) { failRestore(); return; }
  verifiedSource_ = frame.source_frame_serial;
}
void PlayerComponentRecovery::submitted(std::uint64_t generation, std::uint64_t serial)
{
  if (!restoring_ || !frameGateActive_) return;
  const auto now = nowMs();
  if (!validRestore(now) || generation != rendererGeneration_ || runtime_.eventEpoch() != frameEpoch_ ||
      !frameGate_.submitted(saved_.key, attempt_, frameEpoch_, serial, now)) failRestore();
}
void PlayerComponentRecovery::swapped(std::uint64_t generation, std::uint64_t serial)
{
  if (!restoring_ || !frameGateActive_) return;
  const auto now = nowMs();
  if (!validRestore(now) || generation != rendererGeneration_ || runtime_.eventEpoch() != frameEpoch_ ||
      !frameGate_.swapped(saved_.key, attempt_, frameEpoch_, serial, now)) { failRestore(); return; }
  maybeResume(now);
}

void PlayerComponentRecovery::maybeResume(std::int64_t now)
{
  if (!validRestore(now) || !positionReady_ || resumePending_ ||
      !frameGate_.ready(saved_.key, attempt_, frameEpoch_, now)) return;
  if (!policy_.frameObserved(saved_.key.generation, attempt_, verifiedSource_, now) ||
      !runtime_.replaceActiveEntry(saved_.key, saved_.entry, recoveryEntry_, observationResult_.positionMs)) {
    failRestore(); return;
  }
  if (saved_.paused) { completed_ = true; restoring_ = false; return; }
  AutomaticRecovery::Sample sample;
  sample.nowMs = now; sample.session = saved_.key.session; sample.revision = saved_.key.revision;
  sample.generation = saved_.key.generation; sample.desiredPaused = false;
  if (!policy_.mayUnpause(sample)) { failRestore(); return; }
  auto lease = core_->acquireNative();
  int paused = 0; unpauseId_ = (std::uint64_t{5} << 61) | attempt_;
  if (!lease.get() || mpv_set_property_async(lease.get(), unpauseId_, "pause", MPV_FORMAT_FLAG, &paused) < 0) {
    failRestore(); return;
  }
  resumePending_ = true;
}
#endif
