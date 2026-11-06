#include "test_player_native_restore.h"
#include <FixtureRestartAdapter.h>
#include <RestartWire.h>
#include "fixture_restart/OwnedFixtureFile.h"
#include <QCryptographicHash>
#include <QTemporaryFile>

void TestPlayerNativeRestore::fixtureBridgeControl()
{
#ifdef MPVQT_EXTERNAL_EVENTS
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
#ifdef HEALTHY_COPIED_SOURCE_TEST
  player.m_healthyStopClock = [] { return qint64(10); };
  QCOMPARE(bridge.armActual().status, FixtureRestart::RestartChild::ArmResult::Status::Unknown);
  QVERIFY(!player.m_nativeRestore.armed);
  QVERIFY(bridge.bindCleanupGate(gate, 100));
  using Child = FixtureRestart::RestartChild;
  Child::Actions actions; actions.now = [] { return qint64(10); };
  actions.armActual = [&] { return bridge.armActual(); };
  actions.beginCleanup = [&](quint64 sequence, quint64 revision) { return bridge.beginCleanup(sequence, revision, 100); };
  actions.pollCleanup = [] { return Child::CleanupResult{Child::CleanupResult::Status::Pending, {}, FixtureRestart::CancelFailure::None}; };
  Child child(*gate, &player, &player, grant.run, 9, digest, std::move(actions));
  QByteArray response;
  QVERIFY(child.grant(&player, frame, 10, true, response));
  auto claim = std::make_shared<HealthyRetirement::RetirementClaim>(1);
  QVERIFY(player.healthyInstallRetirementClaim(claim, 1));
#else
  QVERIFY(gate->accept(&player, frame, grant.run, digest, 10, true));
  QVERIFY(bridge.arm());
#endif
  QVERIFY(!bridge.load(file.fileName())); QCOMPARE(loads, 0);
  QVERIFY(gate->installPermit(FixtureRestart::encodeRestorePermit(permit), permit, 10));
  QVERIFY(bridge.load(file.fileName())); QCOMPARE(loads, 1);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  loadReplyAndStart(player, 2, 42);
  response.clear();
  QVERIFY(child.control(&player, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 4}), response));
  QVERIFY(response.isEmpty());
  completeStopped(player, 42);
  QCOMPARE(bridge.pollCleanup().status, FixtureRestart::RestartChild::CleanupResult::Status::Stopped);
  QCOMPARE(stops, 0);
#else
  QVERIFY(bridge.cancel()); QCOMPARE(stops, 1);
#endif
  QVERIFY(!bridge.seek()); QVERIFY(!bridge.load(file.fileName()));
  PlayerComponent other; other.m_mpvGeneration = 9;
  FixtureRestart::ChildGate blocked(&other, 100); FixtureRestartAdapter cancelled(other, blocked, 8, 3);
  QVERIFY(cancelled.cancel()); QVERIFY(!blocked.accept(&other, frame, grant.run, digest, 10, true)); QVERIFY(!cancelled.arm());
  PlayerComponent stale; stale.m_mpvGeneration = 10;
  FixtureRestart::ChildGate gate2(&stale, 100); FixtureRestartAdapter staleBridge(stale, gate2, 9, 4);
  stale.m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  QVERIFY(gate2.accept(&stale, frame, grant.run, digest, 10, true)); QVERIFY(!staleBridge.arm());
#endif
}
