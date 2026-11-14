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
  QPointer<PlayerComponent> playerGuard(this);
  QPointer<PlayerComponentRecovery> recoveryGuard(recovery);
  m_recovery = recovery;
  const bool bound = recovery->bind();
  if (!playerGuard) return false;
  if (!bound || !recoveryGuard) {
    m_recovery = nullptr; delete recoveryGuard.data(); return false;
  }
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
bool PlayerComponent::recoveryPreferenceChanged(PlaybackIntent::ChangeKind kind)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) { m_recovery->userChanged(false, kind); return m_recovery->preferenceDeferred(); }
#else
  (void)kind;
#endif
  return false;
}
void PlayerComponent::recoveryReplacementStopResult(bool admitted)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->replacementStopResult(admitted);
#else
  (void)admitted;
#endif
}
bool PlayerComponent::recoveryNativeAvailable() const
{
#if defined(MPVQT_RECOVERY_RUNTIME) && defined(PLAYER_RECOVERY_ACTOR_TEST)
  return m_mpv || (m_recovery && m_recovery->testEnabled_);
#else
  return m_mpv;
#endif
}
bool PlayerComponent::recoveryTestMode() const
{
#if defined(MPVQT_RECOVERY_RUNTIME) && defined(PLAYER_RECOVERY_ACTOR_TEST)
  return m_recovery && m_recovery->testEnabled_;
#else
  return false;
#endif
}
void PlayerComponent::applySelectedStreams()
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (ordinaryTracksDispatch_) { ordinaryTracksDispatch_(m_currentAudioStream, m_currentSubtitleStream); return; }
#endif
  reselectStream(m_currentSubtitleStream, MediaType::Subtitle);
  reselectStream(m_currentAudioStream, MediaType::Audio);
}
int PlayerComponent::applySelectedPause(bool paused)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (ordinaryPauseDispatch_) return ordinaryPauseDispatch_(paused);
#endif
  return m_mpv ? m_mpv->setProperty("pause", paused) : MPV_ERROR_UNINITIALIZED;
}
void PlayerComponent::continuePreloadedHook(quint64 id)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (ordinaryHookDispatch_) { ordinaryHookDispatch_(id); return; }
#endif
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
  nativeCall([id](mpv_handle* handle) { return mpv_hook_continue(handle, id); });
#else
  (void)id;
#endif
}
void PlayerComponent::continuePreloadedHook(const QString& id)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (ordinaryLegacyHookDispatch_) { ordinaryLegacyHookDispatch_(id); return; }
#endif
  if (m_mpv) m_mpv->command(QStringList() << "hook-ack" << id);
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
  applySelectedStreams();
#ifdef MPVQT_RECOVERY_RUNTIME
  if (m_recovery) m_recovery->finishPreload();
#endif
  return true;
}

void PlayerComponent::reselectStream(const QVariant &streamSelection, MediaType target)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (recoveryTestMode() && ordinaryReselectDispatch_) { ordinaryReselectDispatch_(streamSelection, target); return; }
#endif
  if (!m_mpv) {
    qWarning() << "PlayerComponent::reselectStream: mpv not initialized yet";
    return;
  }

  QString streamIdPropertyName;
  QString streamAddCommandName;
  QString mpvStreamTypeName;

  switch (target)
  {
  case MediaType::Subtitle:
    mpvStreamTypeName = "sub";
    streamIdPropertyName = "sid";
    streamAddCommandName = "sub-add";
    break;
  case MediaType::Audio:
    mpvStreamTypeName = "audio";
    streamIdPropertyName = "aid";
    streamAddCommandName = "audio-add";
    break;
  }

  // Check for string format first (handles "#,URL" and "#index")
  QString streamSelectionStr = streamSelection.toString();
  bool isStringFormat = streamSelectionStr.startsWith("#") || !streamSelection.canConvert<int>();

  // Handle integer Jellyfin stream index (new format)
  if (!isStringFormat && streamSelection.canConvert<int>()) {
    int index = streamSelection.toInt();
    if (index < 0) {
      m_mpv->setProperty(streamIdPropertyName, "no");
    } else {
      // Jellyfin stream index is the MPV track ID (already 1-based)
      m_mpv->setProperty(streamIdPropertyName, index);
    }
    return;
  }

  // Handle legacy string format "#1" or "#,http://..."
  QString streamName;
  QString streamID;

  if (streamSelectionStr.startsWith("#"))
  {
    qsizetype splitPos = streamSelectionStr.indexOf(",");
    if (splitPos < 0)
    {
      // Stream from the main file
      streamID = streamSelectionStr.mid(1);
      streamName = "";
    }
    else
    {
      // Stream from an external file
      streamID = streamSelectionStr.mid(1, splitPos - 1);
      streamName = streamSelectionStr.mid(splitPos + 1);
    }
  }
  else if (streamSelectionStr.isEmpty() || !streamSelection.isValid())
  {
    m_mpv->setProperty( streamIdPropertyName, "no");
    return;
  }

  if (!streamName.isEmpty())
  {
    auto streams = findStreamsForURL(streamName);
    if (streams.isEmpty())
    {
      QStringList args = (QStringList() << streamAddCommandName << streamName);
      m_mpv->command( args);
    }
  }

  QString selection = "no";

  if (!streamID.isEmpty())
  {
    selection = streamID;
  } else {
    for (auto stream : findStreamsForURL(streamName))
    {
      auto map = stream.toMap();

      if (map["type"].toString() != mpvStreamTypeName)
      {
        continue;
      } else if (map["external-filename"].toString() == streamName) {
        selection = map["id"].toString();
        break;
      }
    }
  }

  // Fallback to the first stream if none could be found.
  // Useful if web-client uses wrong stream IDs when e.g. transcoding.
  if ((target == MediaType::Audio || !streamID.isEmpty()) && selection == "no")
    selection = "1";

  m_mpv->setProperty( streamIdPropertyName, selection);
}
