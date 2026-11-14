#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RecoveryIdentity.h"
#include "system/WebClientFaultGuard.h"
#include <QQuickWindow>
#include <limits>

bool PlayerComponentRecovery::qualifiedReadStall(AutomaticRecoverySession::Key key,
    const AutomaticRecoveryObservation::Result& result, std::int64_t now, bool movingVideo)
{
  const auto intent = player_->m_playbackIntent.snapshot();
  AutomaticRecoveryReadStall::Sample sample;
  sample.key = key; sample.entry = result.entry; sample.observedMs = result.observedMs; sample.nowMs = now;
  sample.positionMs = result.positionMs; sample.eventEpoch = runtime_.eventEpoch();
  sample.batchSequence = result.batchSequence; sample.sourceFrameSerial = movingVideo ? lastSource_ : 0;
  sample.nativePaused = result.nativePaused; sample.desiredPaused = intent.desired != PlaybackIntent::Desired::Playing;
  sample.seekable = result.seekable; sample.video = result.video && movingVideo; sample.seeking = result.seeking;
  sample.loading = !player_->m_inPlayback;
  sample.buffering = result.buffering; sample.eof = player_->m_state == PlayerComponent::State::finished;
  // A generic native error is not classified as a permanent or network failure.
  sample.errorAbsent = player_->m_playbackError.isEmpty() && player_->m_state != PlayerComponent::State::error;
  sample.visible = player_->m_window && player_->m_window->isVisible() && player_->m_windowVisible && item_->isVisible();
  sample.exposed = player_->m_window && player_->m_window->isExposed();
  return readStall_.observe(sample) == AutomaticRecoveryReadStall::Result::BufferingStall;
}

void PlayerComponentRecovery::watchdogObservation(AutomaticRecoverySession::Key key,
    const AutomaticRecoveryObservation::Result& result, std::int64_t now)
{
  AutomaticRecoverySession::Snapshot snapshot;
  if (!runtime_.snapshot(key, snapshot)) { readStall_.reset(); return; }
  const auto intent = player_->m_playbackIntent.snapshot();
  AutomaticRecovery::Sample sample;
  sample.nowMs = now; sample.positionMs = result.positionMs; sample.session = key.session;
  sample.revision = key.revision; sample.generation = key.generation; sample.frameSerial = lastSource_;
  sample.seekable = result.seekable; sample.video = result.video; sample.nativePaused = result.nativePaused;
  sample.desiredPaused = intent.desired == PlaybackIntent::Desired::Paused;
  sample.loading = !player_->m_inPlayback; sample.seeking = result.seeking; sample.buffering = result.buffering;
  sample.visible = player_->m_window && player_->m_window->isVisible() && player_->m_windowVisible && item_->isVisible();
  sample.exposed = player_->m_window && player_->m_window->isExposed();
  sample.eof = player_->m_state == PlayerComponent::State::finished;
  sample.movingVideo = sourceEntry_ == static_cast<std::uint64_t>(result.entry) && sourceEpoch_ == runtime_.eventEpoch() &&
      sourceGeneration_ == item_->recoveryRendererGeneration() && sourceFirst_ && lastSource_ > sourceFirst_ &&
      player_->m_mediaFrameRate >= 1.0f;
  sample.outstanding = sample.movingVideo;
  sample.readStalled = qualifiedReadStall(key, result, now, sample.movingVideo);
  sample.rendererFault = WebClientFaultGuard::instance().rendererLost();
  const auto action = policy_.observe(sample);
  if (action == AutomaticRecovery::Action::Blocked || action == AutomaticRecovery::Action::Cancel) {
    awaitingUsability_ = false; frameGate_.invalidate(); readStall_.reset(); return;
  }
  if (action != AutomaticRecovery::Action::Reload) return;
  request_ = RecoveryIdentity::take(RecoveryIdentity::Domain::Usability);
  if (!request_) { policy_.fail(); return; }
  saved_ = snapshot; rendererGeneration_ = item_->recoveryRendererGeneration();
  if (!rendererGeneration_ || !armFrameAttempt()) { clearFrameAttempt(); policy_.fail(); return; }
  readStall_.reset(); awaitingUsability_ = true;
  item_->requestRendererUsability(rendererGeneration_, request_, policy_.deadlineMs());
}
#endif
