#include "PlayerComponent.h"
#include "PlaybackCheckpoint.h"
#include <MpvController>
#include <cstring>

namespace {
// MpvQt's public asynchronous IDs are signed int; this range cannot be
// produced by their conversion to uint64_t, including negative IDs.
constexpr quint64 receiptTag = quint64{1} << 63;
std::int64_t entryFromReply(const mpv_event* event)
{
  if (!event->data) return 0;
  const auto& node = static_cast<const mpv_event_command*>(event->data)->result;
  if (node.format != MPV_FORMAT_NODE_MAP || !node.u.list || node.u.list->num < 0 ||
      node.u.list->num > 16 || !node.u.list->keys || !node.u.list->values) return 0;
  std::int64_t found = 0;
  bool seen = false;
  for (int i = 0; i < node.u.list->num; ++i) {
    if (!node.u.list->keys[i] || strcmp(node.u.list->keys[i], "playlist_entry_id")) continue;
    if (seen || node.u.list->values[i].format != MPV_FORMAT_INT64) return 0;
    seen = true;
    found = node.u.list->values[i].u.int64;
  }
  return found > 0 ? found : 0;
}
}

bool PlayerComponent::bindNativeReceiptHost(quint64 host)
{
  if (!intentControlThread() || !host || m_receiptHost || m_receiptHostRevoked) return false;
  m_receiptHost = host;
  return true;
}

void PlayerComponent::revokeNativeReceiptHost()
{
  invalidateNativeReceipt();
  m_receiptHost = 0;
  m_receiptHostRevoked = true;
}

bool PlayerComponent::beginNativeReceipt(FixtureAsset asset, const QString& fixturePath, quint64 host, quint64 attempt,
                                         bool initiallyPaused)
{
#ifndef MPVQT_EXTERNAL_EVENTS
  Q_UNUSED(asset); Q_UNUSED(fixturePath); Q_UNUSED(host); Q_UNUSED(attempt); Q_UNUSED(initiallyPaused);
  return false;
#else
  if (!intentControlThread() || asset != FixtureAsset::TestClip || fixturePath.isEmpty() || !host || host != m_receiptHost || !m_mpvGeneration || attempt <= m_lastReceiptAttempt ||
      attempt > PlaybackIntent::MaxSafeInteger) return false;
  const auto intent = m_playbackIntent.snapshot();
  if (!intent.epoch || !intent.request || !intent.revision ||
      (intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused)) return false;
  if (intent.revision == m_lastReceiptRevision) return false;
  const auto previous = m_nativeReceipts.snapshot();
  if (previous.consumed && !previous.submissionObserved) return false;
  if (previous.submitted && !previous.settled) return false;
  if (previous.key.attempt && !previous.invalid) { invalidateNativeReceipt(); return false; }
  const PlaybackNativeReceipts::Key key{host, m_mpvGeneration, intent.revision, attempt};
  m_lastReceiptAttempt = attempt;
  m_lastReceiptRevision = intent.revision;
  m_nativeReceipts = PlaybackNativeReceipts{};
  m_fixturePosition = -1;
  m_fixtureCheckpointConsumed = false;
  if (!m_nativeReceipts.begin(key) || !m_nativeReceipts.consume(key)) return false;
  int result = -1;
  if (initiallyPaused) {
    const auto path = fixturePath.toUtf8();
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2, 3)
    const char* args[] = {"loadfile", path.constData(), "replace", "-1", "pause=yes", nullptr};
#else
    const char* args[] = {"loadfile", path.constData(), "replace", "pause=yes", nullptr};
#endif
    if (m_restoreDispatch) result = m_restoreDispatch(receiptTag | attempt, args);
    else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), receiptTag | attempt, args);
  } else if (m_receiptDispatch) {
    result = m_receiptDispatch(attempt, fixturePath);
  } else if (m_mpv && m_mpv->mpv()) {
    const auto path = fixturePath.toUtf8();
    const char* args[] = {"loadfile", path.constData(), "replace", nullptr};
    result = mpv_command_async(m_mpv->mpv(), receiptTag | attempt, args);
  }
  m_nativeReceipts.submitted(result);
  return result >= 0;
#endif
}

bool PlayerComponent::consumeFixtureCheckpoint(PlaybackNativeReceipts::Key key, const QByteArray& checkpoint)
{
  if (!intentControlThread() || m_fixtureCheckpointConsumed || !fixturePositionCurrent()) return false;
  const auto receipt = m_nativeReceipts.snapshot();
  if (key.host != receipt.key.host || key.core != receipt.key.core || key.revision != receipt.key.revision ||
      key.attempt != receipt.key.attempt) return false;
  m_fixtureCheckpointConsumed = true;
  QByteArray expected;
  PlaybackCheckpoint decoded;
  return PlaybackCheckpointCodec::encode(makePlaybackCheckpoint(), expected) &&
         PlaybackCheckpointCodec::decode(checkpoint, decoded) && checkpoint == expected;
}

void PlayerComponent::invalidateNativeReceipt()
{
  m_fixturePosition = -1;
  m_nativeObservation.valid = false;
  m_nativeReceipts.invalidate();
}

bool PlayerComponent::fixturePositionCurrent() const
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto receipt = m_nativeReceipts.snapshot();
  return (m_fixturePosition >= 0 || nativeObservationCurrent()) && !receipt.invalid && receipt.replyOk && receipt.started && !receipt.ended &&
         receipt.key.host == m_receiptHost && receipt.key.core == m_mpvGeneration &&
         receipt.key.revision == m_playbackIntent.snapshot().revision;
#else
  return false;
#endif
}

// This private injection seam models a host-verified fixture observation; it is
// not wired to global mpv property notifications or exposed through WebChannel.
bool PlayerComponent::recordFixturePosition(FixtureAsset asset, PlaybackNativeReceipts::Key key, qint64 entry, qint64 positionMs)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto receipt = m_nativeReceipts.snapshot();
  if (!intentControlThread() || asset != FixtureAsset::TestClip || positionMs < 0 || quint64(positionMs) > PlaybackIntent::MaxSafeInteger ||
      receipt.invalid || !receipt.replyOk || !receipt.started || receipt.ended || entry != receipt.entry ||
      key.host != receipt.key.host || key.core != receipt.key.core || key.revision != receipt.key.revision || key.attempt != receipt.key.attempt ||
      key.host != m_receiptHost || key.core != m_mpvGeneration || key.revision != m_playbackIntent.snapshot().revision) return false;
  m_fixturePosition = positionMs;
  m_nativeObservation.valid = false;
  return true;
#else
  Q_UNUSED(asset); Q_UNUSED(key); Q_UNUSED(entry); Q_UNUSED(positionMs);
  return false;
#endif
}

void PlayerComponent::observeNativeReceipt(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  observeNativeObservation(event);
  if (!event || !m_nativeReceipts.snapshot().key.attempt) return;
  const auto receipt = m_nativeReceipts.snapshot();
  if (receipt.key.core != m_mpvGeneration) {
    invalidateNativeReceipt();
    return;
  }
  if (receipt.key.revision != m_playbackIntent.snapshot().revision) invalidateNativeReceipt();
  switch (event->event_id) {
  case MPV_EVENT_COMMAND_REPLY:
    if (event->reply_userdata == (receiptTag | receipt.key.attempt))
      m_nativeReceipts.reply(receipt.key.attempt, event->error >= 0, entryFromReply(event));
    break;
  case MPV_EVENT_START_FILE:
    m_nativeReceipts.start(event->data ? static_cast<mpv_event_start_file*>(event->data)->playlist_entry_id : 0);
    break;
  case MPV_EVENT_END_FILE:
    if (event->data) {
      const auto* end = static_cast<mpv_event_end_file*>(event->data);
      m_nativeReceipts.end(end->playlist_entry_id, end->reason == MPV_END_FILE_REASON_REDIRECT);
    } else invalidateNativeReceipt();
    break;
  case MPV_EVENT_QUEUE_OVERFLOW:
  case MPV_EVENT_SHUTDOWN: invalidateNativeReceipt(); break;
  default: break;
  }
  observeNativeRestore(event);
#else
  Q_UNUSED(event);
#endif
}
