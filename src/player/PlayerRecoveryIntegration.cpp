#include "PlayerComponent.h"
#include <MpvController>
#ifdef MPVQT_RECOVERY_RUNTIME
#include "PlayerComponentRecovery.h"
#endif

bool PlayerComponent::setupRecovery()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) return true;
  auto* recovery = new PlayerComponentRecovery(this);
  if (!recovery->bind()) { delete recovery; return false; }
  m_recovery = recovery;
#endif
  return true;
}

void PlayerComponent::releaseRecovery()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) { m_recovery->shutdown(); delete m_recovery; m_recovery = nullptr; }
#endif
}

void PlayerComponent::recoveryIntentChanged(bool clear)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->userChanged(clear);
#else
  (void)clear;
#endif
}
void PlayerComponent::recoveryUserReplacement()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->beginUserReplacement();
#endif
}
void PlayerComponent::noteQueueIntent(bool paused)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_playbackIntent.snapshot().desired == PlaybackIntent::Desired::Stopped) m_playbackIntent.play(paused);
  else m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
#else
  (void)paused;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
#endif
  recoveryIntentChanged(false);
}

bool PlayerComponent::dispatchPlayerLoad(const QStringList& command, const QString& url, const QVariantMap& options,
                                         const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) return m_recovery->queue(command, url, options, metadata, audio, subtitle);
#else
  (void)url; (void)options; (void)metadata; (void)audio; (void)subtitle;
#endif
  m_mpv->command(command); return true;
}

bool PlayerComponent::applyPreloadedStreams()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery && m_recovery->restoring() && !m_recovery->prepareRecoveryPreload()) return false;
#endif
  reselectStream(m_currentSubtitleStream, MediaType::Subtitle);
  reselectStream(m_currentAudioStream, MediaType::Audio);
  return true;
}
