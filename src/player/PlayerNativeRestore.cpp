#include "PlayerComponent.h"
#include <MpvController>

namespace {
constexpr quint64 seekTag = quint64{1} << 61;
bool sameKey(PlaybackNativeReceipts::Key a, PlaybackNativeReceipts::Key b)
{
  return a.host == b.host && a.core == b.core && a.revision == b.revision && a.attempt == b.attempt;
}
}

bool PlayerComponent::armNativeRestore(const RestoreGrant& grant, quint64 host, quint64 attempt)
{
#ifndef MPVQT_EXTERNAL_EVENTS
  Q_UNUSED(grant); Q_UNUSED(host); Q_UNUSED(attempt);
  return false;
#else
  if (!intentControlThread() || m_nativeRestore.armed || !grant.id || !grant.parentRevision ||
      grant.positionMs < 0 || grant.positionMs > FixtureDurationMs ||
      !host || host != m_receiptHost || !m_mpvGeneration || !attempt || attempt > PlaybackIntent::MaxSafeInteger)
    return false;
  const auto previous = m_playbackIntent.snapshot();
  if (previous.revision != grant.expectedRevision || previous.desired == PlaybackIntent::Desired::Exiting) return false;
  m_playbackIntent.play(grant.desiredPaused);
  const auto intent = m_playbackIntent.snapshot();
  if (!intent.request || !intent.revision || intent.desired == PlaybackIntent::Desired::Exiting) return false;
  m_nativeRestore.grant = grant;
  m_nativeRestore.key = {host, m_mpvGeneration, intent.revision, attempt};
  m_nativeRestore.armed = true;
  return true;
#endif
}

bool PlayerComponent::dispatchNativeRestoreLoad(const QString& fixturePath)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto key = m_nativeRestore.key;
  if (!intentControlThread() || !m_nativeRestore.armed || m_nativeRestore.cancelled ||
      m_nativeRestore.loadAttempted || key.host != m_receiptHost || key.core != m_mpvGeneration ||
      key.revision != m_playbackIntent.snapshot().revision) return false;
  m_nativeRestore.loadAttempted = true;
  return beginNativeReceipt(FixtureAsset::TestClip, fixturePath, key.host, key.attempt, true);
#else
  Q_UNUSED(fixturePath); return false;
#endif
}

bool PlayerComponent::dispatchNativeRestoreSeek()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto receipt = m_nativeReceipts.snapshot();
  const auto key = m_nativeRestore.key;
  if (!intentControlThread() || !m_nativeRestore.armed || m_nativeRestore.cancelled ||
      m_nativeRestore.seekPending || m_nativeRestore.seekReply || !sameKey(key, receipt.key) ||
      receipt.invalid || !receipt.replyOk || !receipt.started || receipt.ended ||
      !nativeObservationCurrent() || !m_nativeObservation.paused || key.host != m_receiptHost ||
      key.core != m_mpvGeneration || key.revision != m_playbackIntent.snapshot().revision) return false;
  m_nativeRestore.seekPending = true;
  m_nativeObservation.valid = false;
  const qint64 ms = m_nativeRestore.grant.positionMs;
  const QByteArray position = QByteArray::number(ms / 1000) + '.' + QByteArray::number(ms % 1000).rightJustified(3, '0');
  const char* args[] = {"seek", position.constData(), "absolute+exact", nullptr};
  int result = -1;
  if (m_restoreDispatch) result = m_restoreDispatch(seekTag | key.attempt, args);
  else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), seekTag | key.attempt, args);
  m_nativeRestore.seekAccepted = result >= 0;
  if (result < 0) { m_nativeRestore.seekPending = false; m_nativeRestore.cancelled = true; }
  return result >= 0;
#else
  return false;
#endif
}

PlayerComponent::NativeStopResult PlayerComponent::cancelNativeRestore()
{
  if (!intentControlThread() || !m_nativeRestore.armed || m_nativeRestore.cancelled)
    return {NativeStopResult::Status::Unavailable, 0};
  m_nativeRestore.cancelled = true;
  invalidateNativeReceipt();
  m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  return m_nativeRestore.loadAttempted ? stopMpv() : NativeStopResult{NativeStopResult::Status::Unavailable, 0};
}

void PlayerComponent::observeNativeRestore(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!event || !m_nativeRestore.armed) return;
  const auto key = m_nativeRestore.key;
  const auto receipt = m_nativeReceipts.snapshot();
  if (!sameKey(key, receipt.key) || key.core != m_mpvGeneration) return;
  if (event->event_id == MPV_EVENT_COMMAND_REPLY && m_nativeRestore.seekPending &&
      event->reply_userdata == (seekTag | key.attempt)) {
    m_nativeRestore.seekPending = false;
    m_nativeRestore.seekReply = true;
    m_nativeRestore.seekOk = m_nativeRestore.seekAccepted && event->error >= 0;
  } else if (event->event_id == MPV_EVENT_PLAYBACK_RESTART && m_nativeRestore.seekOk &&
             !m_nativeRestore.cancelled && !receipt.invalid && !receipt.ended && key.host == m_receiptHost &&
             key.revision == m_playbackIntent.snapshot().revision) {
    m_nativeRestore.restarted = true;
  }
#else
  Q_UNUSED(event);
#endif
}
