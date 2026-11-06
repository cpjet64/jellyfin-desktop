#include "player/PlayerComponent.h"
#include "player/PlaybackCheckpoint.h"
#include <QtTest/QtTest>

class TestPlayerNativeRestore : public QObject
{
  Q_OBJECT
private:
  static void prepare(PlayerComponent& player);
  static void observationReply(PlayerComponent& player, quint64 id, const QByteArray& text, int error = 0);
  static void loadReplyAndStart(PlayerComponent& player, quint64 attempt, qint64 entry);
private slots:
  void actualObservationBoundary();
  void lateAndDuplicateObservation();
  void pausedLoadAndExactSeek();
  void cancellationBeforeLoad();
  void staleObservationAndGrant();
  void cancellationAfterLoad();
};

void TestPlayerNativeRestore::loadReplyAndStart(PlayerComponent& player, quint64 attempt, qint64 entry)
{
  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = entry;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 63) | attempt; event.data = &command;
  player.observeNativeReceipt(&event);
  mpv_event_start_file start{}; start.playlist_entry_id = entry;
  event.event_id = MPV_EVENT_START_FILE; event.data = &start;
  player.observeNativeReceipt(&event);
}

void TestPlayerNativeRestore::prepare(PlayerComponent& player)
{
  player.bindNativeReceiptHost(4);
  player.m_playbackIntent.play(false);
#ifdef MPVQT_EXTERNAL_EVENTS
  player.m_mpvGeneration = 3;
  const auto revision = player.m_playbackIntent.snapshot().revision;
  const PlaybackNativeReceipts::Key key{4, 3, revision, 1};
  player.m_nativeReceipts.begin(key);
  player.m_nativeReceipts.consume(key);
  player.m_nativeReceipts.submitted(0);
  player.m_nativeReceipts.reply(1, true, 42);
  player.m_nativeReceipts.start(42);
#endif
}

void TestPlayerNativeRestore::observationReply(PlayerComponent& player, quint64 id, const QByteArray& text, int error)
{
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_STRING;
  command.result.u.string = const_cast<char*>(text.constData());
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | id;
  event.error = error;
  event.data = &command;
  player.observeNativeReceipt(&event);
}

void TestPlayerNativeRestore::actualObservationBoundary()
{
  PlayerComponent player;
  prepare(player);
  int calls = 0;
  bool commandMatches = false;
  player.m_observationDispatch = [&](quint64 id, const char** args) {
    ++calls;
    commandMatches = id == ((quint64{1} << 62) | 7) && QByteArray(args[0]) == "expand-text" &&
                     QByteArray(args[1]) == NativePlaybackObservation::expression();
    return 0;
  };
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(!player.requestNativeObservation(key, 7, 30000, true));
  QVERIFY(player.requestNativeObservation(key, 7, 10000, true));
  QVERIFY(commandMatches);
  QVERIFY(!player.requestNativeObservation(key, 8, 10000, true));
  observationReply(player, 7, "NR1|1|42|yes|yes|3.125|yes|no|no");
  QVERIFY(player.m_nativeObservation.valid);
  QVERIFY(player.m_nativeObservation.requestedNs <= player.m_nativeObservation.receivedNs);
  auto candidate = player.makePlaybackCheckpoint();
  QVERIFY(candidate.positionKnown && candidate.positionMs == 3125);
  QVERIFY(!player.m_playbackIntent.automaticResumeEligible());
  QVERIFY(player.requestNativeObservation(key, 8, 10000, true));
  observationReply(player, 8, "NR1|1|42|yes|yes|10.001|yes|no|no");
  QVERIFY(!player.m_nativeObservation.valid);
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);
  QCOMPARE(calls, 2);
#else
  QVERIFY(!player.requestNativeObservation({}, 7, 10000, true));
  QCOMPARE(calls, 0);
#endif
}

void TestPlayerNativeRestore::lateAndDuplicateObservation()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  prepare(player);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  player.requestNativeStop();
  observationReply(player, 1, "NR1|1|42|yes|yes|10|yes|no|no");
  QVERIFY(!player.m_nativeObservation.valid);
  observationReply(player, 1, "NR1|1|42|yes|yes|10|yes|no|no");
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);
#endif
}

void TestPlayerNativeRestore::pausedLoadAndExactSeek()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  PlayerComponent::RestoreGrant grant{3, 4, 0, 3125, false};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  QList<QByteArray> loadArgs, seekArgs;
  player.m_restoreDispatch = [&](quint64 id, const char** args) {
    auto& out = id & (quint64{1} << 63) ? loadArgs : seekArgs;
    for (int i = 0; args[i]; ++i) out.append(args[i]);
    return 0;
  };
  QVERIFY(!player.dispatchNativeRestoreSeek());
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  QCOMPARE(loadArgs.first(), QByteArray("loadfile"));
  QCOMPARE(loadArgs.last(), QByteArray("pause=yes"));
  loadReplyAndStart(player, 2, 42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 9, 10000, true));
  observationReply(player, 9, "NR1|1|42|yes|yes|0|yes|no|no");
  QVERIFY(player.dispatchNativeRestoreSeek());
  QCOMPARE(seekArgs, QList<QByteArray>({"seek", "3.125", "absolute+exact"}));
  QVERIFY(!player.m_nativeObservation.valid);
  mpv_event event{}; event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted);
  event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 61) | 2;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.seekReply && player.m_nativeRestore.seekOk);
  event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.restarted);
#endif
}

void TestPlayerNativeRestore::cancellationBeforeLoad()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  PlayerComponent::RestoreGrant grant{3, 4, 0, 1000, true};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  int calls = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++calls; return 0; };
  player.cancelNativeRestore();
  QVERIFY(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  QCOMPARE(calls, 0);
#endif
}

void TestPlayerNativeRestore::staleObservationAndGrant()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  prepare(player);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  observationReply(player, 1, "NR1|1|42|yes|yes|1|yes|no|no");
  QVERIFY(player.makePlaybackCheckpoint().positionKnown);
  ++player.m_nativeObservation.key.attempt;
  QVERIFY(!player.makePlaybackCheckpoint().positionKnown);

  PlayerComponent stale;
  QVERIFY(stale.bindNativeReceiptHost(7));
  stale.m_mpvGeneration = 8;
  PlayerComponent::RestoreGrant grant{3, 4, 0, 1000, true};
  stale.requestNativeStop();
  const auto revision = stale.m_playbackIntent.snapshot().revision;
  QVERIFY(!stale.armNativeRestore(grant, 7, 2));
  QCOMPARE(stale.m_playbackIntent.snapshot().revision, revision);
  QCOMPARE(stale.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Stopped);
#endif
}

void TestPlayerNativeRestore::cancellationAfterLoad()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, false};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  int commands = 0, stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++commands; return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  loadReplyAndStart(player, 2, 42);
  const auto stopped = player.cancelNativeRestore();
  QCOMPARE(stopped.status, PlayerComponent::NativeStopResult::Status::Accepted);
  QVERIFY(!player.dispatchNativeRestoreSeek());
  player.cancelNativeRestore();
  QCOMPARE(stops, 1);
  QCOMPARE(commands, 1);
#endif
}

QTEST_GUILESS_MAIN(TestPlayerNativeRestore)
#include "test_player_native_restore.moc"
