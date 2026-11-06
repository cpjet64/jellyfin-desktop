#include <fixture_restart/FixtureRestartAdapter.h>
#include <fixture_restart/RestartWire.h>
#include <player/PlayerComponent.h>
#include "player_test_cache.h"
#include <QCryptographicHash>
#include <QtTest/QtTest>
#include <mpv/client.h>
#include <cstring>
#include <memory>

class TestRestartChildPlayer : public QObject {
  Q_OBJECT
  PlayerTestCache cache_;
private slots:
  void actualArmAndNoLoadCleanup();
  void wrongGateAndExpiredDeadline();
};

static bool stopGate(void*) { return true; }
static FixtureRestart::Grant grant()
{ return {1, 2, 3, QByteArray(64, 'a'), 3000, true}; }
static QByteArray digest()
{ return QCryptographicHash::hash(FixtureRestart::encodeGrant(grant()), QCryptographicHash::Sha256); }

void TestRestartChildPlayer::actualArmAndNoLoadCleanup()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; player.m_mpvGeneration = 8;
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  player.m_healthyStopClock = [] { return qint64(10); };
  quint64 idle = 0, position = 0;
  player.m_healthyStopDispatch = [&](quint64 id, const char** args, bool) {
    if (!std::strcmp(args[0], "idle-active")) idle = id;
    if (!std::strcmp(args[0], "playlist-pos")) position = id;
    return 0;
  };
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  FixtureRestartAdapter adapter(player, *gate, 7, 2, 9, digest(), [] { return qint64(10); });
  QVERIFY(adapter.bindCleanupGate(gate));
  QVERIFY(gate->accept(&player, FixtureRestart::encodeGrant(grant()), 1, digest(), 10, true));
  const auto armed = adapter.armActual();
  QCOMPARE(armed.status, FixtureRestart::RestartChild::ArmResult::Status::Armed);
  QCOMPARE(armed.identity.host, quint64(7)); QCOMPARE(armed.identity.core, quint64(8));
  QCOMPARE(armed.identity.attempt, quint64(2)); QVERIFY(armed.identity.intentRevision);
  QVERIFY(!adapter.seek()); // renderer-issued target capability is not wired
  QVERIFY(gate->cancel(stopGate, nullptr));
  QVERIFY(adapter.beginCleanup(1, 4, 100));
  QCOMPARE(adapter.pollCleanup().status, FixtureRestart::RestartChild::CleanupResult::Status::Pending);
  QVERIFY(idle && position);
  int idleValue = 1; mpv_event_property idleProperty{"idle-active", MPV_FORMAT_FLAG, &idleValue};
  mpv_event idleReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, idle, &idleProperty};
  player.observeNativeRestoreCleanup(&idleReply);
  int64_t noEntry = -1; mpv_event_property positionProperty{"playlist-pos", MPV_FORMAT_INT64, &noEntry};
  mpv_event positionReply{MPV_EVENT_GET_PROPERTY_REPLY, 0, position, &positionProperty};
  player.observeNativeRestoreCleanup(&positionReply);
  const auto result = adapter.pollCleanup();
  QCOMPARE(result.status, FixtureRestart::RestartChild::CleanupResult::Status::NoLoad);
  QCOMPARE(result.identity.attempt, armed.identity.attempt);
  QVERIFY(!adapter.load(QStringLiteral("unused")) && !adapter.seek());
#endif
}

void TestRestartChildPlayer::wrongGateAndExpiredDeadline()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; player.m_mpvGeneration = 8;
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  player.m_healthyStopClock = [] { return qint64(100); };
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  auto wrong = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  FixtureRestartAdapter adapter(player, *gate, 7, 2, 9, digest(), [] { return qint64(10); });
  QVERIFY(!adapter.bindCleanupGate(wrong)); QVERIFY(adapter.bindCleanupGate(gate));
  QVERIFY(gate->accept(&player, FixtureRestart::encodeGrant(grant()), 1, digest(), 10, true));
  QCOMPARE(adapter.armActual().status, FixtureRestart::RestartChild::ArmResult::Status::Armed);
  QVERIFY(gate->cancel(stopGate, nullptr)); QVERIFY(!adapter.beginCleanup(1, 4, 100));
  QCOMPARE(adapter.pollCleanup().status, FixtureRestart::RestartChild::CleanupResult::Status::Failed);
#endif
}

QTEST_GUILESS_MAIN(TestRestartChildPlayer)
#include "test_fixture_adapter_cleanup.moc"
