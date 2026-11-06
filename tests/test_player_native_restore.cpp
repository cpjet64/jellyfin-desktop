#include "player/PlayerComponent.h"
#include "player/PlaybackCheckpoint.h"
#include "player_test_cache.h"
#include "fixture_restart/FixtureRestartAdapter.h"
#include <QCryptographicHash>
#include "fixture_restart/OwnedFixtureFile.h"
#include <QTemporaryFile>
#include <QtTest/QtTest>

class TestPlayerNativeRestore : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
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
  void fixtureBridgeControl();
  void fixtureBridgeCapture();
  void unavailableSystemMode();
  void lateAfterEndAndCoreChange();
  void malformedAndUnrelatedReplies();
  void reorderedLoadAndNativeConsumption();
  void cancellationDuringSeek();
  void reentrantCancellationAndSeekFailure();
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

void TestPlayerNativeRestore::unavailableSystemMode()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  const auto before = player.m_playbackIntent.snapshot();
  int calls = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++calls; return 0; };
  const PlayerComponent::RestoreGrant grant{3, 4, before.revision, 1000, true};
  QVERIFY(!player.armNativeRestore(grant, 7, 2));
  QVERIFY(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  QVERIFY(!player.dispatchNativeRestoreSeek());
  QCOMPARE(calls, 0);
  const auto after = player.m_playbackIntent.snapshot();
  QCOMPARE(after.revision, before.revision);
  QCOMPARE(after.desired, before.desired);
#endif
}

void TestPlayerNativeRestore::lateAfterEndAndCoreChange()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent ended;
  prepare(ended);
  ended.m_observationDispatch = [](quint64, const char**) { return 0; };
  const auto key = ended.m_nativeReceipts.snapshot().key;
  QVERIFY(ended.requestNativeObservation(key, 1, 10000, true));
  mpv_event_end_file data{}; data.playlist_entry_id = 42; data.reason = MPV_END_FILE_REASON_STOP;
  mpv_event event{}; event.event_id = MPV_EVENT_END_FILE; event.data = &data;
  ended.observeNativeReceipt(&event);
  observationReply(ended, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!ended.nativeObservationCurrent());
  QVERIFY(!ended.makePlaybackCheckpoint().positionKnown);

  PlayerComponent changed;
  prepare(changed);
  changed.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(changed.requestNativeObservation(changed.m_nativeReceipts.snapshot().key, 1, 10000, true));
  ++changed.m_mpvGeneration;
  observationReply(changed, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!changed.nativeObservationCurrent());
  QVERIFY(!changed.makePlaybackCheckpoint().positionKnown);

  PlayerComponent replaced;
  prepare(replaced);
  replaced.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(replaced.requestNativeObservation(replaced.m_nativeReceipts.snapshot().key, 1, 10000, true));
  replaced.load(QStringLiteral("new"), {{"autoplay", true}}, {});
  observationReply(replaced, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!replaced.nativeObservationCurrent());
  QVERIFY(!replaced.makePlaybackCheckpoint().positionKnown);
#endif
}

void TestPlayerNativeRestore::malformedAndUnrelatedReplies()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  prepare(player);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  const auto key = player.m_nativeReceipts.snapshot().key;
  const QByteArray invalid[] = {"NR1|1|41|yes|yes|3|yes|no|no", "NR1|1|42|yes|yes|ERR|yes|no|no",
                                "NR1|1|42|yes|yes|NaN|yes|no|no", "NR1|1|42|yes|yes|3|yes|no|no|extra"};
  quint64 id = 1;
  for (const auto& value : invalid) {
    QVERIFY(player.requestNativeObservation(key, id, 10000, true));
    observationReply(player, id + 100, "NR1|1|42|yes|yes|3|yes|no|no");
    QVERIFY(player.m_nativeObservation.pending);
    observationReply(player, id, value);
    QVERIFY(!player.nativeObservationCurrent());
    QVERIFY(!player.makePlaybackCheckpoint().positionKnown);
    ++id;
  }
#endif
}

void TestPlayerNativeRestore::reorderedLoadAndNativeConsumption()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3125, false};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  mpv_event_start_file start{}; start.playlist_entry_id = 42;
  mpv_event event{}; event.event_id = MPV_EVENT_START_FILE; event.data = &start;
  player.observeNativeReceipt(&event);
  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = 42;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 63) | 2; event.data = &command;
  player.observeNativeReceipt(&event);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.m_nativeReceipts.snapshot().started);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  observationReply(player, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QByteArray encoded;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), encoded));
  QVERIFY(player.consumeFixtureCheckpoint(key, encoded));
  QVERIFY(!player.consumeFixtureCheckpoint(key, encoded));
  player.requestNativeStop();
  QVERIFY(!player.consumeFixtureCheckpoint(key, encoded));
#endif
}

void TestPlayerNativeRestore::cancellationDuringSeek()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3125, false};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  int commands = 0, stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++commands; return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  loadReplyAndStart(player, 2, 42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  observationReply(player, 1, "NR1|1|42|yes|yes|0|yes|no|no");
  QVERIFY(player.dispatchNativeRestoreSeek());
  player.cancelNativeRestore();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 61) | 2;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.cancelled && player.m_nativeRestore.seekReply);
  event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted);
  QVERIFY(!player.dispatchNativeRestoreSeek());
  QCOMPARE(commands, 2);
  QCOMPARE(stops, 1);
#endif
}

void TestPlayerNativeRestore::reentrantCancellationAndSeekFailure()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent observation;
  prepare(observation);
  observation.m_observationDispatch = [&](quint64, const char**) { observation.requestNativeStop(); return 0; };
  const auto observationKey = observation.m_nativeReceipts.snapshot().key;
  QVERIFY(observation.requestNativeObservation(observationKey, 1, 10000, true));
  observationReply(observation, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!observation.nativeObservationCurrent());

  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3125, false};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  int commands = 0, stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  player.m_restoreDispatch = [&](quint64, const char**) {
    ++commands;
    if (commands == 2) player.cancelNativeRestore();
    return 0;
  };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  loadReplyAndStart(player, 2, 42);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, 1, 10000, true));
  observationReply(player, 1, "NR1|1|42|yes|yes|0|yes|no|no");
  QVERIFY(player.dispatchNativeRestoreSeek());
  QVERIFY(player.m_nativeRestore.cancelled && player.m_nativeRestore.seekPending);
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 61) | 2;
  event.error = -1;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.seekReply && !player.m_nativeRestore.seekOk);
  event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted);
  QCOMPARE(commands, 2);
  QCOMPARE(stops, 1);
#endif
}

void TestPlayerNativeRestore::fixtureBridgeControl()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  const QByteArray data("fixture");
  QCOMPARE(file.write(data), qint64(data.size()));
  QVERIFY(file.flush());
  FixtureRestart::Grant grant{1, 2, 3, QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(), 3125, false};
  PlayerComponent player;
  player.m_mpvGeneration = 8;
  int stops = 0, loads = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++loads; return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  QVERIFY(gate.accept(&player, FixtureRestart::encodeGrant(grant), grant, 10, true));
  QVERIFY(bridge.arm());
  QVERIFY(bridge.load(file.fileName()));
  QCOMPARE(loads, 1);
  QVERIFY(bridge.cancel());
  QCOMPARE(stops, 1);
  QVERIFY(!bridge.seek());
  QVERIFY(!bridge.load(file.fileName()));
  PlayerComponent other;
  other.m_mpvGeneration = 9;
  FixtureRestart::ChildGate blocked(&other, 100);
  FixtureRestartAdapter cancelled(other, blocked, 8, 3);
  QVERIFY(cancelled.cancel());
  QVERIFY(!blocked.accept(&other, FixtureRestart::encodeGrant(grant), grant, 10, true));
  QVERIFY(!cancelled.arm());
  PlayerComponent stale;
  stale.m_mpvGeneration = 10;
  FixtureRestart::ChildGate gate2(&stale, 100);
  FixtureRestartAdapter staleBridge(stale, gate2, 9, 4);
  stale.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  QVERIFY(gate2.accept(&stale, FixtureRestart::encodeGrant(grant), grant, 10, true));
  QVERIFY(!staleBridge.arm());
#endif
}

void TestPlayerNativeRestore::fixtureBridgeCapture()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  player.m_mpvGeneration = 3;
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  const QByteArray data("fixture-a");
  QCOMPARE(file.write(data), qint64(data.size()));
  QVERIFY(file.flush());
  const auto hash = QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex();
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter adapter(player, gate, 4, 1);
  QVERIFY(adapter.loadA(file.fileName(), hash, false));
  loadReplyAndStart(player, 1, 42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  FixtureRestart::Grant binding{1, 2, 3, hash, 0, false};
  FixtureRestart::Evidence frozen;
  QVERIFY(player.recordFixturePosition(PlayerComponent::FixtureAsset::TestClip, key, 42, 3000));
  QVERIFY(!adapter.captureA(binding, frozen));
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 9, 10000, false));
  observationReply(player, 9, "NR1|1|42|yes|yes|3|no|no|no");
  QVERIFY(adapter.captureA(binding, frozen));
  QVERIFY(FixtureRestart::validateFixtureEvidence(frozen));
  QVERIFY(file.seek(file.size()));
  QCOMPARE(file.write("x", 1), qint64(1));
  QVERIFY(file.flush());
  QVERIFY(!adapter.internalReplacementStop(frozen));
  QVERIFY(file.resize(data.size()));
  QVERIFY(file.flush());
  const auto revision = player.m_playbackIntent.snapshot().revision;
  player.m_stopDispatch = [] { return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(adapter.internalReplacementStop(frozen));
  QCOMPARE(player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
  QVERIFY(!adapter.internalReplacementStop(frozen));
#endif
}

QTEST_GUILESS_MAIN(TestPlayerNativeRestore)
#include "test_player_native_restore.moc"
