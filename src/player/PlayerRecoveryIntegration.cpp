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
  reselectStream(m_currentSubtitleStream, MediaType::Subtitle);
  reselectStream(m_currentAudioStream, MediaType::Audio);
}

int PlayerComponent::ordinaryProperty(const QString& name, const QVariant& value)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (recoveryTestMode() && ordinaryPropertyDispatch_) return ordinaryPropertyDispatch_(name, value);
#endif
  return m_mpv ? m_mpv->setProperty(name, value) : MPV_ERROR_UNINITIALIZED;
}

void PlayerComponent::ordinaryPlayerCommand(const QStringList& args)
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (recoveryTestMode() && ordinaryPlayerCommandDispatch_) { ordinaryPlayerCommandDispatch_(args); return; }
#endif
  if (m_mpv) m_mpv->command(args);
}

void PlayerComponent::reselectStream(const QVariant& selection, MediaType target)
{
  if (!recoveryNativeAvailable()) {
    qWarning() << "PlayerComponent::reselectStream: mpv not initialized yet"; return;
  }
  const bool subtitle = target == MediaType::Subtitle;
  const QString property = subtitle ? "sid" : "aid";
  const QString value = selection.toString();
  if (!value.startsWith('#') && selection.canConvert<int>()) {
    const int index = selection.toInt();
    ordinaryProperty(property, index < 0 ? QVariant("no") : QVariant(index)); return;
  }
  selectLegacyStream(selection, target, property, subtitle ? "sub-add" : "audio-add", subtitle ? "sub" : "audio");
}

void PlayerComponent::selectLegacyStream(const QVariant& selection, MediaType target, const QString& property,
                                         const QString& addCommand, const QString& streamType)
{
  const QString value = selection.toString();
  QString streamId, streamName;
  if (value.startsWith('#')) {
    const qsizetype split = value.indexOf(',');
    streamId = split < 0 ? value.mid(1) : value.mid(1, split - 1);
    if (split >= 0) streamName = value.mid(split + 1);
  } else if (value.isEmpty() || !selection.isValid()) {
    ordinaryProperty(property, "no"); return;
  }
  if (!streamName.isEmpty() && findStreamsForURL(streamName).isEmpty())
    ordinaryPlayerCommand(QStringList() << addCommand << streamName);
  QString result = "no";
  if (!streamId.isEmpty()) result = streamId;
  else for (const auto& stream : findStreamsForURL(streamName)) {
    const auto map = stream.toMap();
    if (map["type"].toString() == streamType && map["external-filename"].toString() == streamName) {
      result = map["id"].toString(); break;
    }
  }
  if ((target == MediaType::Audio || !streamId.isEmpty()) && result == "no") result = "1";
  ordinaryProperty(property, result);
}
int PlayerComponent::applySelectedPause(bool paused) { return ordinaryProperty("pause", paused); }
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
