#include "PlayerComponent.h"
#include <MpvController>
#include "utils/Log.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "PlayerComponentRecovery.h"
#endif

int PlayerComponent::nativeCall(const std::function<int(mpv_handle*)>& operation) const
{
  if (!operation) return MPV_ERROR_UNINITIALIZED;
#ifdef MPVQT_RECOVERY_RUNTIME
  if (!m_recovery) return MPV_ERROR_UNINITIALIZED;
  auto lease = m_recovery->acquireNative();
  return lease.get() ? operation(lease.get()) : MPV_ERROR_UNINITIALIZED;
#else
  return m_mpv && m_mpv->mpv() ? operation(m_mpv->mpv()) : MPV_ERROR_UNINITIALIZED;
#endif
}

int PlayerComponent::nativeAsyncCommand(quint64 id, const char** args) const
{ return nativeCall([id, args](mpv_handle* handle) { return mpv_command_async(handle, id, args); }); }

QString PlayerComponent::nativeOsd(const QString& property) const
{
  QString result = "-";
  nativeCall([&](mpv_handle* handle) -> int {
    char* value = mpv_get_property_osd_string(handle, property.toUtf8().constData());
    if (!value) return MPV_ERROR_PROPERTY_UNAVAILABLE;
    result = QString::fromUtf8(value); mpv_free(value); return 0;
  });
  if (result.size() > 400) result = result.left(400) + "...";
  Log::CensorAuthTokens(result);
  return result;
}

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

bool PlayerComponent::recoveryIntentChanged(bool clear)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  return !m_recovery || m_recovery->userChanged(clear);
#else
  (void)clear; return true;
#endif
}
void PlayerComponent::recoveryUserReplacement()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->beginUserReplacement();
#endif
}
bool PlayerComponent::cancelRecoverySeekBeforeIntent()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  return !m_recovery || m_recovery->cancelBeforeIntent();
#else
  return true;
#endif
}
bool PlayerComponent::noteQueueIntent(bool paused)
{
  if (!cancelRecoverySeekBeforeIntent()) return false;
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_playbackIntent.snapshot().desired == PlaybackIntent::Desired::Stopped) m_playbackIntent.play(paused);
  else m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
#else
  (void)paused;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
#endif
  return recoveryIntentChanged(false);
}
void PlayerComponent::prepareRecoverySeek(qint64 targetMs)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->prepareOwnedSeek(targetMs);
#else
  (void)targetMs;
#endif
}
int PlayerComponent::dispatchRecoverySeek(qint64 targetMs)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  return m_recovery ? m_recovery->dispatchOwnedSeek(targetMs) : 0;
#else
  (void)targetMs; return 0;
#endif
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

bool PlayerComponent::applyPreloadedStreams(const QString& hookToken)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery && !m_recovery->preparePreload(hookToken)) return false;
#else
  (void)hookToken;
#endif
  reselectStream(m_currentSubtitleStream, MediaType::Subtitle);
  reselectStream(m_currentAudioStream, MediaType::Audio);
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->finishPreload();
#endif
  return true;
}
