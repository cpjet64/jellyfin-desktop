#include "test_player_native_restore.h"
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <restore_test_fixture.h>
#include "player/PlaybackCheckpoint.h"
#include <QScopeGuard>
#include <vector>

void TestPlayerNativeRestore::completeGraphStopped(PlayerComponent& player, qint64 entry)
{
  quint64 idle = 0, position = 0; std::vector<mpv_event*> queue; std::size_t index = 0;
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&]() { return index < queue.size() ? queue[index++] : &none; };
  player.m_healthyStopDispatch = [&](quint64 id, const char** args, bool) {
    if (QByteArray(args[0]) == "idle-active") idle = id;
    if (QByteArray(args[0]) == "playlist-pos") position = id;
    return 0;
  };
  const auto cleanup = qScopeGuard([&player] { player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {}; });
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  mpv_event stop{}; stop.event_id = MPV_EVENT_COMMAND_REPLY;
  stop.reply_userdata = (quint64{1} << 55) | player.m_nativeRestore.key.attempt;
  mpv_event_end_file data{}; data.playlist_entry_id = entry; data.reason = MPV_END_FILE_REASON_STOP;
  mpv_event end{}; end.event_id = MPV_EVENT_END_FILE; end.data = &data;
  queue = {&stop, &end}; index = 0;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QVERIFY(idle && position);
  int idleValue = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idleValue};
  int64_t noEntry = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &noEntry};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, idle, &idleProperty};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, position, &positionProperty};
  queue = {&idleReply, &positionReply}; index = 0;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Stopped);
}

void TestPlayerNativeRestore::pausedLoadAndExactSeek()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  auto& player = *fixture.player(); QVERIFY(!fixture.seek());
  QVERIFY(fixture.load()); QCOMPARE(fixture.state()->loads, 1u); QVERIFY(fixture.state()->pausedLoad);
  QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(9, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek());
  QCOMPARE(fixture.state()->seeks, 1u);
  const auto& args = fixture.state()->lastArguments;
  QCOMPARE(args.size(), size_t{3}); QCOMPARE(args[0], QByteArray("seek"));
  QCOMPARE(args[1], QByteArray("3.000")); QCOMPARE(args[2], QByteArray("absolute+exact"));
  QVERIFY(!player.m_nativeObservation.valid);
  mpv_event event{}; event.event_id = MPV_EVENT_PLAYBACK_RESTART; player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted);
  fixture.replySeekAndRestart(0, true, false);
  QVERIFY(player.m_nativeRestore.seekReply && player.m_nativeRestore.seekOk);
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.restarted);
}

void TestPlayerNativeRestore::diagnosticPositionRejected(qint64 position)
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, position)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(!fixture.armTarget());
  QVERIFY(!fixture.seek()); QCOMPARE(fixture.state()->loads, 1u);
  QCOMPARE(fixture.state()->seeks, 0u); QCOMPARE(fixture.state()->stops, 0u);
}
void TestPlayerNativeRestore::diagnosticPosition1000Rejected() { diagnosticPositionRejected(1000); }
void TestPlayerNativeRestore::diagnosticPosition3125Rejected() { diagnosticPositionRejected(3125); }

void TestPlayerNativeRestore::diagnosticReentrantObservation()
{
  PlayerComponent player; prepare(player);
  player.m_observationDispatch = [&](quint64, const char**) { player.requestNativeStop(); return 0; };
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  observationReply(player, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!player.nativeObservationCurrent());
}

void TestPlayerNativeRestore::cancellationBeforeLoad()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(true, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QByteArray response; QVERIFY(fixture.cancel(2, 5, response));
  QVERIFY(!fixture.load()); QCOMPARE(fixture.state()->loads, 0u); QCOMPARE(fixture.state()->stops, 0u);
  auto& player = *fixture.player();
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QVERIFY(fixture.state()->idleQuery && fixture.state()->positionQuery);
  int idle = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idle};
  int64_t position = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &position};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, fixture.state()->idleQuery, &idleProperty};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, fixture.state()->positionQuery, &positionProperty};
  player.observeNativeRestoreCleanup(&idleReply); player.observeNativeReceipt(&idleReply);
  player.observeNativeRestoreCleanup(&positionReply); player.observeNativeReceipt(&positionReply);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::NoLoad);
  QCOMPARE(fixture.state()->stops, 0u);
}

void TestPlayerNativeRestore::cancellationAfterLoad()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QCOMPARE(fixture.state()->loads, 1u);
  QByteArray response; QVERIFY(fixture.cancel(2, 5, response)); QVERIFY(!fixture.seek());
  completeGraphStopped(*fixture.player(), 42);
  QCOMPARE(fixture.state()->stops, 1u); QCOMPARE(fixture.state()->seeks, 0u);
}

void TestPlayerNativeRestore::reorderedLoadAndNativeConsumption()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42, true));
  auto& player = *fixture.player(); const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.m_nativeReceipts.snapshot().started); QVERIFY(fixture.observe(1, 42, "3"));
  QByteArray encoded; QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), encoded));
  QVERIFY(player.consumeFixtureCheckpoint(key, encoded)); QVERIFY(!player.consumeFixtureCheckpoint(key, encoded));
  player.requestNativeStop(); QVERIFY(!player.consumeFixtureCheckpoint(key, encoded));
  QCOMPARE(fixture.state()->loads, 1u); QCOMPARE(fixture.state()->seeks, 0u);
}

void TestPlayerNativeRestore::cancellationDuringSeek()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek());
  QByteArray response; QVERIFY(fixture.cancel(2, 5, response));
  auto& player = *fixture.player(); fixture.replySeekAndRestart(0, false, false);
  QVERIFY(player.m_nativeRestore.cancelled && player.m_nativeRestore.seekReply);
  mpv_event event{}; event.event_id = MPV_EVENT_PLAYBACK_RESTART; player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted); QVERIFY(!fixture.seek());
  QCOMPARE(fixture.state()->loads, 1u); QCOMPARE(fixture.state()->seeks, 1u);
  completeGraphStopped(player, 42); QCOMPARE(fixture.state()->stops, 1u);
}

void TestPlayerNativeRestore::reentrantCancellationAndSeekFailure()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(false, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget());
  bool canceled = false; QByteArray response;
  fixture.state()->submissionHook = [&] { canceled = fixture.cancel(2, 5, response); };
  const auto cleanup = qScopeGuard([state = fixture.state()] { state->submissionHook = {}; });
  QVERIFY(!fixture.seek()); QVERIFY(canceled);
  auto& player = *fixture.player(); QVERIFY(player.m_nativeRestore.cancelled && player.m_nativeRestore.seekPending);
  fixture.replySeekAndRestart(-1, false, false);
  QVERIFY(player.m_nativeRestore.seekReply && !player.m_nativeRestore.seekOk);
  mpv_event event{}; event.event_id = MPV_EVENT_PLAYBACK_RESTART; player.observeNativeReceipt(&event);
  QVERIFY(!player.m_nativeRestore.restarted);
  QCOMPARE(fixture.state()->loads, 1u); QCOMPARE(fixture.state()->seeks, 1u);
  QCOMPARE(fixture.state()->stops, 0u);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(fixture.state()->stops, 1u);
}
#endif
