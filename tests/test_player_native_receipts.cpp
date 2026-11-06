#include "player/PlayerComponent.h"
#include "player/PlaybackCheckpoint.h"
#include <QtTest/QtTest>

class TestPlayerNativeReceipts : public QObject
{
  Q_OBJECT
private:
  static void reply(PlayerComponent& player, quint64 attempt, qint64 entry, int error = 0);
  static void start(PlayerComponent& player, qint64 entry);
  private slots:
  void supportedBoundary();
  void rejectionAndStaleIntent();
  void replyBeforeStartAndOldCore();
  void malformedReplyAndStop();
  void redirectAndOverflow();
  void captureInvalidation();
};

void TestPlayerNativeReceipts::reply(PlayerComponent& player, quint64 attempt, qint64 entry, int error)
{
  mpv_node value{};
  value.format = MPV_FORMAT_INT64;
  value.u.int64 = entry;
  char name[] = "playlist_entry_id";
  char* names[] = {name};
  mpv_node_list list{};
  list.num = 1; list.keys = names; list.values = &value;
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 63) | attempt;
  event.error = error; event.data = &command;
  player.observeNativeReceipt(&event);
}

void TestPlayerNativeReceipts::start(PlayerComponent& player, qint64 entry)
{
  mpv_event_start_file start{};
  start.playlist_entry_id = entry;
  mpv_event event{};
  event.event_id = MPV_EVENT_START_FILE; event.data = &start;
  player.observeNativeReceipt(&event);
}

void TestPlayerNativeReceipts::supportedBoundary()
{
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(1));
  player.m_playbackIntent.play(false);
  int calls = 0;
  player.m_receiptDispatch = [&](quint64, const QString&) { ++calls; return 0; };
#ifdef MPVQT_EXTERNAL_EVENTS
  player.m_mpvGeneration = 4;
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 7));
  QCOMPARE(calls, 1);
  start(player, 42);
  reply(player, 7, 42);
  const auto receipt = player.m_nativeReceipts.snapshot();
  QVERIFY(receipt.replyOk && receipt.started && !receipt.invalid);
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);
  QVERIFY(!player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, receipt.key, 43, 900));
  QVERIFY(player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, receipt.key, 42, 900));
  QByteArray encoded;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), encoded));
  PlaybackCheckpoint decoded;
  QVERIFY(PlaybackCheckpointCodec::decode(encoded, decoded));
  QVERIFY(decoded.positionKnown && decoded.positionMs == 900);
  auto wrong = receipt.key;
  ++wrong.host;
  QVERIFY(!player.consumeFixtureCheckpoint(wrong, encoded));
  QVERIFY(player.consumeFixtureCheckpoint(receipt.key, encoded));
  QVERIFY(!player.consumeFixtureCheckpoint(receipt.key, encoded));
  QVERIFY(!player.m_playbackIntent.automaticResumeEligible());
  QVERIFY(!player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 7));
  QCOMPARE(calls, 1);
#else
  QVERIFY(!player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 7));
  QCOMPARE(calls, 0);
#endif
}

void TestPlayerNativeReceipts::rejectionAndStaleIntent()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(1));
  player.m_mpvGeneration = 3;
  player.m_playbackIntent.play(false);
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 9));
  reply(player, 9, 15);
  player.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  start(player, 15);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);

  PlayerComponent rejected;
  QVERIFY(rejected.bindNativeReceiptHost(1));
  rejected.m_mpvGeneration = 4;
  rejected.m_playbackIntent.play(false);
  rejected.m_receiptDispatch = [](quint64, const QString&) { return -1; };
  QVERIFY(!rejected.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 10));
  QVERIFY(rejected.m_nativeReceipts.snapshot().invalid);
  QVERIFY(!rejected.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 10));
  QVERIFY(!rejected.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 11));

  PlayerComponent asyncFailure;
  QVERIFY(asyncFailure.bindNativeReceiptHost(1));
  asyncFailure.m_mpvGeneration = 5;
  asyncFailure.m_playbackIntent.play(false);
  asyncFailure.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(asyncFailure.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 12));
  reply(asyncFailure, 12, 0, -1);
  const auto failure = asyncFailure.m_nativeReceipts.snapshot();
  QVERIFY(failure.submitted && failure.reply && !failure.replyOk && failure.settled && failure.invalid);
#endif
}

void TestPlayerNativeReceipts::replyBeforeStartAndOldCore()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(1));
  player.m_mpvGeneration = 3;
  player.m_playbackIntent.play(false);
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 11));
  reply(player, 11, 50);
  QVERIFY(!player.m_nativeReceipts.snapshot().started);
  start(player, 50);
  QVERIFY(player.m_nativeReceipts.snapshot().started);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 50, 100));
  QVERIFY(!player.consumeFixtureCheckpoint(key, QByteArray("{}")));
  QByteArray valid;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), valid));
  QVERIFY(!player.consumeFixtureCheckpoint(key, valid));
  player.m_mpvGeneration = 4;
  QVERIFY(!player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 50, 100));
  start(player, 50);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
#endif
}

void TestPlayerNativeReceipts::malformedReplyAndStop()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent malformed;
  QVERIFY(malformed.bindNativeReceiptHost(1));
  malformed.m_mpvGeneration = 3;
  malformed.m_playbackIntent.play(false);
  malformed.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(malformed.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 12));
  reply(malformed, 12, 0);
  QVERIFY(malformed.m_nativeReceipts.snapshot().invalid);

  PlayerComponent stopped;
  QVERIFY(stopped.bindNativeReceiptHost(1));
  stopped.m_mpvGeneration = 5;
  stopped.m_playbackIntent.play(false);
  stopped.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(stopped.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 13));
  stopped.requestNativeStop();
  QVERIFY(stopped.m_nativeReceipts.snapshot().invalid);
  reply(stopped, 13, 55);
  QVERIFY(!stopped.makePlaybackCheckpoint().positionKnown);
  stopped.m_playbackIntent.play(false);
  QVERIFY(!stopped.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 14));
#endif
}

void TestPlayerNativeReceipts::redirectAndOverflow()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(1));
  player.m_mpvGeneration = 1;
  player.m_playbackIntent.play(false);
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(!player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 2, 1));
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 1));
  start(player, 18);
  mpv_event_end_file end{};
  end.playlist_entry_id = 18; end.reason = MPV_END_FILE_REASON_REDIRECT;
  mpv_event event{};
  event.event_id = MPV_EVENT_END_FILE; event.data = &end;
  player.observeNativeReceipt(&event);
  reply(player, 1, 18);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);
  player.m_playbackIntent.play(false);
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 2));
  event.event_id = MPV_EVENT_QUEUE_OVERFLOW; event.data = nullptr;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
#endif
}

void TestPlayerNativeReceipts::captureInvalidation()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(1));
  player.m_mpvGeneration = 1;
  player.m_playbackIntent.play(false);
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  PlaybackNativeReceipts::Key fabricated{1, 1, player.m_playbackIntent.snapshot().revision, 1};
  QByteArray unbound;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), unbound));
  QVERIFY(!player.consumeFixtureCheckpoint(fabricated, unbound));
  QVERIFY(player.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 1, 1));
  reply(player, 1, 20);
  start(player, 20);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(!player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 19, 200));
  QVERIFY(player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 20, 200));
  QByteArray captured;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), captured));
  player.load(QStringLiteral("new"), {{"autoplay", true}}, {});
  QVERIFY(!player.consumeFixtureCheckpoint(key, captured));
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);

  PlayerComponent stopped;
  QVERIFY(stopped.bindNativeReceiptHost(2));
  stopped.m_mpvGeneration = 2;
  stopped.m_playbackIntent.play(false);
  stopped.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(stopped.beginNativeReceipt(PlayerComponent::FixtureAsset::TestClip, QStringLiteral("fixture"), 2, 2));
  reply(stopped, 2, 21);
  start(stopped, 21);
  const auto stoppedKey = stopped.m_nativeReceipts.snapshot().key;
  QVERIFY(stopped.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, stoppedKey, 21, 200));
  QVERIFY(PlaybackCheckpointCodec::encode(stopped.makePlaybackCheckpoint(), captured));
  stopped.requestNativeStop();
  QVERIFY(!stopped.consumeFixtureCheckpoint(stoppedKey, captured));
#endif
}

QTEST_GUILESS_MAIN(TestPlayerNativeReceipts)
#include "test_player_native_receipts.moc"
