#include "PlayerComponent.h"
#include "AlbumArtProvider.h"
#include "PlaybackCheckpoint.h"
#include "input/InputComponent.h"
#include "system/WebClientFaultGuard.h"
#include <MpvController>
#include <QDebug>
#include <QThread>
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
#include "PlayerIntentObservation.h"
#define PLAYER_QUEUE_DISPATCH(call) (PlayerIntentObservation::queueDispatch(), (call))
#else
#define PLAYER_QUEUE_DISPATCH(call) (call)
#endif

PlaybackCheckpoint PlayerComponent::makePlaybackCheckpoint() const
{
  PlaybackCheckpoint candidate;
  candidate.intent = m_playbackIntent.snapshot();
  if (nativeObservationCurrent()) {
    candidate.intent.timelineQualified = true;
    candidate.positionKnown = true;
    candidate.positionMs = m_nativeObservation.positionMs;
    candidate.item = QStringLiteral("fixture-clip");
    candidate.source = QStringLiteral("fixture-source");
    return candidate;
  }
  if (fixturePositionCurrent()) {
    candidate.intent.timelineQualified = true;
    candidate.positionKnown = true;
    candidate.positionMs = m_fixturePosition;
    candidate.item = QStringLiteral("fixture-clip");
    candidate.source = QStringLiteral("fixture-source");
  }
  return candidate;
}

void PlayerComponent::setWebPlaylist(const QVariantList& playlist, const QString& currentItemId)
{
  if (!intentControlThread()) return;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  m_webPlaylist = playlist;
  m_currentWebPlaylistItemId = currentItemId;
  emit webPlaylistChanged(playlist, currentItemId);
}

void PlayerComponent::clearQueue()
{
  if (!intentControlThread()) return;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  if (!m_mpv) {
    qWarning() << "PlayerComponent::clearQueue: mpv not initialized yet";
    return;
  }
  QStringList args("playlist_clear");
  m_mpv->command(args);
}

void PlayerComponent::queueMedia(const QString& url, const QVariantMap& options, const QVariantMap &metadata, const QVariant& audioStream, const QVariant& subtitleStream)
{
  if (!intentControlThread()) return;
  const bool replacement = m_replacementLoad;
  m_replacementLoad = false;
  if (!replacement) m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  const auto revision = m_playbackIntent.snapshot().revision;
  if (!m_mpv) {
    m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Unavailable);
    qWarning() << "PlayerComponent::queueMedia: mpv not initialized yet";
    return;
  }

  InputComponent::Get().cancelAutoRepeat();

  m_mediaFrameRate = metadata["frameRate"].toFloat(); // returns 0 on failure
  m_serverMediaInfo = metadata["media"].toMap();

  updateVideoConfiguration();

  QUrl qurl = url;
  QString host = qurl.host();

  QStringList command;
  command << "loadfile" << qurl.toString(QUrl::FullyEncoded);
  command << "append-play"; // if nothing is playing, play it now, otherwise just enqueue it

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2, 3)
  command << "-1"; // insert_at_idx
#endif

  QString extraArgs;

  quint64 startMilliseconds = options["startMilliseconds"].toLongLong();
  if (startMilliseconds != 0)
    extraArgs += "start=+" + QString::number(startMilliseconds / 1000.0) + ",";

  // we're going to select these streams later, in the preloaded hook
  extraArgs += "aid=no,";
  extraArgs += "sid=no,";

  m_currentSubtitleStream = subtitleStream;
  m_currentAudioStream = audioStream;

  if (metadata["type"] == "music")
    extraArgs += "vid=no,";

  QString pause = options["autoplay"].toBool() ? "no" : "yes";
  extraArgs += "pause=" + pause + ",";

  QString userAgent = metadata["headers"].toMap()["User-Agent"].toString();
  if (userAgent.size())
    extraArgs += "user-agent=" + userAgent + ",";

  // Make sure the list of requested codecs is reset.
  extraArgs += "ad=,";
  extraArgs += "vd="; // Keep last

  command << extraArgs;

  PLAYER_QUEUE_DISPATCH(m_mpv->command( command));
  m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Queued);

  QVariantMap jellyfinMetadata = metadata["metadata"].toMap();
  QUrl jellyfinBaseUrl = qurl.adjusted(QUrl::RemovePath | QUrl::RemoveQuery);
  emit onMetaData(jellyfinMetadata, jellyfinBaseUrl);

  // Request album art from the provider
  if (m_albumArtProvider)
    m_albumArtProvider->requestArtwork(jellyfinMetadata, jellyfinBaseUrl);
}

void PlayerComponent::play()
{
  if (!intentControlThread()) return;
  m_playbackIntent.resume();
  const auto revision = m_playbackIntent.snapshot().revision;
  if (!m_mpv) {
    m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Unavailable);
    qWarning() << "PlayerComponent::play: mpv not initialized yet";
    return;
  }
  QStringList args = (QStringList() << "set" << "pause" << "no");
  m_mpv->command( args);
  m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Queued);
}

void PlayerComponent::pause()
{
  if (!intentControlThread()) return;
  m_playbackIntent.pause();
  const auto revision = m_playbackIntent.snapshot().revision;
  if (!m_mpv) {
    m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Unavailable);
    qWarning() << "PlayerComponent::pause: mpv not initialized yet";
    return;
  }
  QStringList args = (QStringList() << "set" << "pause" << "yes");
  m_mpv->command( args);
  m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Queued);
}

void PlayerComponent::seekTo(qint64 ms)
{
  if (!intentControlThread()) return;
  m_playbackIntent.seek(ms);
  const auto revision = m_playbackIntent.snapshot().revision;
  if (!m_mpv) {
    m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Unavailable);
    qWarning() << "PlayerComponent::seekTo: mpv not initialized yet";
    return;
  }
  double timeSecs = ms / 1000.0;
  QStringList args = (QStringList() << "seek" << QString::number(timeSecs) << "absolute+exact");
  m_mpv->command( args);
  m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Queued);
}

PlayerComponent::NativeSeekResult PlayerComponent::requestNativeSeek(qint64 milliseconds)
{
  if (QThread::currentThread() != thread()) return {NativeSeekResult::Status::Unavailable, 0};
  m_playbackIntent.seek(milliseconds);
  const auto revision = m_playbackIntent.snapshot().revision;
  if (WebClientFaultGuard::instance().rendererLost() || !m_mpv || !m_mpv->mpv() || milliseconds < 0)
    { m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::Unavailable); return {NativeSeekResult::Status::Unavailable, 0}; }
  const auto result = checkedSeek(m_mpv->mpv(), milliseconds, mpv_command);
  m_playbackIntent.dispatch(revision, result.status == NativeSeekResult::Status::Accepted ?
    PlaybackIntent::Dispatch::NativeAccepted : PlaybackIntent::Dispatch::NativeRejected);
  return result;
}

PlayerComponent::NativeSeekResult PlayerComponent::checkedSeek(mpv_handle* handle, qint64 milliseconds,
                                                                int (*command)(mpv_handle*, const char**))
{
  const QByteArray position = QString::number(milliseconds / 1000.0).toUtf8();
  const char* args[] = {"seek", position.constData(), "absolute+exact", nullptr};
  const int error = command(handle, args);
  return {error < 0 ? NativeSeekResult::Status::Error : NativeSeekResult::Status::Accepted, error};
}

void PlayerComponent::setSubtitleStream(const QVariant &subtitleStream)
{
  if (!intentControlThread()) return;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  m_currentSubtitleStream = subtitleStream;
  reselectStream(m_currentSubtitleStream, MediaType::Subtitle);
}

void PlayerComponent::setAudioStream(const QVariant &audioStream)
{
  if (!intentControlThread()) return;
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  m_currentAudioStream = audioStream;
  reselectStream(m_currentAudioStream, MediaType::Audio);
}

void PlayerComponent::handleMpvEvents()
{
  if (!intentControlThread()) return;
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!m_mpv) return;
#endif
  // Process all events, until the event queue is empty.
  while (1)
  {
    mpv_event *event = mpv_wait_event(m_mpv->mpv(), 0);
    if (event->event_id == MPV_EVENT_NONE)
      break;
    observeNativeReceipt(event);
    if (event->event_id == MPV_EVENT_END_FILE)
      m_playbackIntent.invalidate(PlaybackIntent::Reason::UnattributedEvent);
    handleMpvEvent(event);
  }
  // Once we got all status updates, determine the new canonical state.
  updatePlaybackState();
}
