#include "PlayerComponent.h"
#include <MpvController>
#include <chrono>

namespace {
constexpr quint64 observationTag = quint64{1} << 62;
qint64 monotonicNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool sameKey(PlaybackNativeReceipts::Key a, PlaybackNativeReceipts::Key b)
{
  return a.host == b.host && a.core == b.core && a.revision == b.revision && a.attempt == b.attempt;
}
}

bool PlayerComponent::requestNativeObservation(PlaybackNativeReceipts::Key key, quint64 id, qint64 durationMs, bool paused)
{
#ifndef MPVQT_EXTERNAL_EVENTS
  Q_UNUSED(key); Q_UNUSED(id); Q_UNUSED(durationMs); Q_UNUSED(paused);
  return false;
#else
  const auto receipt = m_nativeReceipts.snapshot();
  if (!intentControlThread() || m_nativeObservation.pending || id <= m_lastObservationId ||
      id > PlaybackIntent::MaxSafeInteger || durationMs != FixtureDurationMs || !sameKey(key, receipt.key) ||
      receipt.invalid || !receipt.replyOk || !receipt.started || receipt.ended ||
      key.host != m_receiptHost || key.core != m_mpvGeneration ||
      key.revision != m_playbackIntent.snapshot().revision) return false;
  m_lastObservationId = id;
  m_fixturePosition = -1;
  m_nativeObservation = {};
  m_nativeObservation.key = key;
  m_nativeObservation.id = id;
  m_nativeObservation.durationMs = durationMs;
  m_nativeObservation.paused = paused;
  m_nativeObservation.pending = true;
  m_nativeObservation.requestedNs = monotonicNs();
  const char* args[] = {"expand-text", NativePlaybackObservation::expression(), nullptr};
  int result = -1;
  if (m_observationDispatch) result = m_observationDispatch(observationTag | id, args);
  else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), observationTag | id, args);
  m_nativeObservation.accepted = result >= 0;
  if (result < 0) m_nativeObservation.pending = false;
  return result >= 0;
#endif
}

bool PlayerComponent::nativeObservationCurrent() const
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto receipt = m_nativeReceipts.snapshot();
  return m_nativeObservation.valid && sameKey(m_nativeObservation.key, receipt.key) && !receipt.invalid &&
         receipt.replyOk && receipt.started && !receipt.ended && receipt.key.host == m_receiptHost &&
         receipt.key.core == m_mpvGeneration && receipt.key.revision == m_playbackIntent.snapshot().revision;
#else
  return false;
#endif
}

void PlayerComponent::observeNativeObservation(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!event || event->event_id != MPV_EVENT_COMMAND_REPLY || !m_nativeObservation.pending ||
      event->reply_userdata != (observationTag | m_nativeObservation.id)) return;
  m_nativeObservation.receivedNs = monotonicNs();
  m_nativeObservation.pending = false;
  const auto receipt = m_nativeReceipts.snapshot();
  if (!m_nativeObservation.accepted || event->error < 0 || !event->data || !sameKey(m_nativeObservation.key, receipt.key) ||
      receipt.invalid || !receipt.replyOk || !receipt.started || receipt.ended ||
      receipt.key.host != m_receiptHost || receipt.key.core != m_mpvGeneration ||
      receipt.key.revision != m_playbackIntent.snapshot().revision) return;
  const auto& result = static_cast<const mpv_event_command*>(event->data)->result;
  if (result.format != MPV_FORMAT_STRING || !result.u.string) return;
  int length = 0;
  while (length <= NativePlaybackObservation::MaxReplyBytes && result.u.string[length]) ++length;
  if (length > NativePlaybackObservation::MaxReplyBytes) return;
  qint64 position = -1;
  if (!NativePlaybackObservation::decode(QByteArray(result.u.string, length), receipt.entry,
                                         m_nativeObservation.durationMs, m_nativeObservation.paused, position)) return;
  m_nativeObservation.positionMs = position;
  m_nativeObservation.valid = true;
#else
  Q_UNUSED(event);
#endif
}
