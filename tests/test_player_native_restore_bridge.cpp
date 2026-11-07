#include "test_player_native_restore.h"
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <restore_test_fixture.h>
#include <QScopeGuard>
#endif
#include <FixtureRestartAdapter.h>
#include <RestartWire.h>
#include "fixture_restart/OwnedFixtureFile.h"
#include <QCryptographicHash>
#include <QTemporaryFile>

void TestPlayerNativeRestore::fixtureBridgeControl()
{
#ifdef MPVQT_EXTERNAL_EVENTS
#ifdef HEALTHY_COPIED_SOURCE_TEST
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(true, 3000));
  QVERIFY(fixture.acceptGrant()); QVERIFY(!fixture.grantAccepted().isEmpty());
  QVERIFY(!fixture.load()); QCOMPARE(fixture.state()->loads, 0u);
  QVERIFY(fixture.installPermit()); QVERIFY(fixture.load());
  QCOMPARE(fixture.state()->loads, 1u); QVERIFY(fixture.state()->pausedLoad);
  auto& player = *fixture.player();
  const auto key = player.m_nativeRestore.key;
  QVERIFY(key.host && key.core && key.revision && key.attempt);
  auto ordinaryStops = std::make_shared<unsigned>(0);
  player.m_stopDispatch = [ordinaryStops] {
    ++*ordinaryStops;
    return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0};
  };
  const auto clearCallbacks = qScopeGuard([&player] {
    player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {}; player.m_stopDispatch = {};
  });
  QVERIFY(fixture.replyLoad(42));
  QByteArray response; QVERIFY(fixture.cancel(2, 5, response)); QVERIFY(response.isEmpty());
  QVERIFY(fixture.state()->stops <= 1u); QCOMPARE(*ordinaryStops, 0u);
  completeGraphStopped(player, 42);
  QVERIFY(fixture.pollCancellation(response));
  const FixtureRestart::CancelResult expected{fixture.grant().run, 5, fixture.grant().grant,
      fixture.grant().revision, 2, key.host, key.core, key.revision, key.attempt,
      FixtureRestart::CancelOutcome::Stopped, FixtureRestart::CancelFailure::None};
  QCOMPARE(response, FixtureRestart::encodeCancelResult(expected));
  QCOMPARE(fixture.state()->stops, 1u); QCOMPARE(*ordinaryStops, 0u);
  const auto current = player.m_nativeRestore.key;
  QCOMPARE(current.host, key.host); QCOMPARE(current.core, key.core);
  QCOMPARE(current.revision, key.revision); QCOMPARE(current.attempt, key.attempt);
  QVERIFY(!fixture.load()); QVERIFY(!fixture.seek()); QVERIFY(!fixture.prepared()->closedAck());
  const auto grant = fixture.grant();
  const auto frame = FixtureRestart::encodeGrant(grant);
  const auto digest = fixture.digest();
#else
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate()); QVERIFY(file.open());
  const QByteArray data("fixture"); QCOMPARE(file.write(data), qint64(data.size())); QVERIFY(file.flush());
  FixtureRestart::Grant grant{1, 2, 3, QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex(), 3125, false};
  PlayerComponent player; player.m_mpvGeneration = 8;
  int stops = 0, loads = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++loads; return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  const auto frame = FixtureRestart::encodeGrant(grant);
  const auto digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  const FixtureRestart::RestorePermit permit{grant.run, 9, grant.grant, grant.revision, 1, digest};
  FixtureRestartAdapter bridge(player, *gate, 7, 2, 9, digest, [] { return 10; });
  QVERIFY(gate->accept(&player, frame, grant.run, digest, 10, true));
  QVERIFY(bridge.arm());
  QVERIFY(!bridge.load(file.fileName())); QCOMPARE(loads, 0);
  QVERIFY(gate->installPermit(FixtureRestart::encodeRestorePermit(permit), permit, 10));
  QVERIFY(bridge.load(file.fileName())); QCOMPARE(loads, 1);
  QVERIFY(bridge.cancel()); QCOMPARE(stops, 1);
  QVERIFY(!bridge.seek()); QVERIFY(!bridge.load(file.fileName()));
#endif
  PlayerComponent other; other.m_mpvGeneration = 9;
  FixtureRestart::ChildGate blocked(&other, 100); FixtureRestartAdapter cancelled(other, blocked, 8, 3);
  QVERIFY(cancelled.cancel()); QVERIFY(!blocked.accept(&other, frame, grant.run, digest, 10, true)); QVERIFY(!cancelled.arm());
  PlayerComponent stale; stale.m_mpvGeneration = 10;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  auto gate2 = std::make_shared<FixtureRestart::ChildGate>(&stale, 100);
  FixtureRestartAdapter staleBridge(stale, *gate2, 9, 4);
  stale.m_healthyStopClock = [] { return qint64(10); };
  QVERIFY(staleBridge.bindCleanupGate(gate2, 100));
  stale.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  QVERIFY(gate2->accept(&stale, frame, grant.run, digest, 10, true)); QVERIFY(!staleBridge.arm());
#else
  FixtureRestart::ChildGate gate2(&stale, 100); FixtureRestartAdapter staleBridge(stale, gate2, 9, 4);
  stale.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  QVERIFY(gate2.accept(&stale, frame, grant.run, digest, 10, true)); QVERIFY(!staleBridge.arm());
#endif
#endif
}
