#include "fixture_restart/FixtureRestartAdapter.h"
#include "fixture_restart/RestartChildPlayer.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCryptographicHash>
#include "fixture_restart/OwnedFixtureFile.h"
#include <QTemporaryFile>
#include <QtTest/QtTest>

class TestRestartChildPlayer : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private slots:
  void cancelBeforeGrant();
  void cancelAfterLoad();
  void reentrantCancel();
  void staleCancel();
  void systemUnavailable();
};

static FixtureRestart::Grant grantFor(const QByteArray& content)
{
  return {1, 2, 3, QCryptographicHash::hash(content, QCryptographicHash::Sha256).toHex(), 3000, true};
}

void TestRestartChildPlayer::cancelBeforeGrant()
{
  PlayerComponent player;
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  int controlChannel;
  const auto grant = grantFor("fixture");
  FixtureRestart::RestartChildPlayer child(bridge, gate, &player, &controlChannel, grant, {});
  QByteArray ack;
  QVERIFY(child.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 4}), ack));
  QVERIFY(!ack.isEmpty());
  QVERIFY(!child.grant(&player, FixtureRestart::encodeGrant(grant), 10, true));
  QVERIFY(!child.load() && !child.seek());
}

void TestRestartChildPlayer::cancelAfterLoad()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  QCOMPARE(file.write("fixture", 7), qint64(7));
  QVERIFY(file.flush());
  PlayerComponent player;
  player.m_mpvGeneration = 8;
  int stops = 0, loads = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++loads; return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  int controlChannel;
  const auto grant = grantFor("fixture");
  FixtureRestart::RestartChildPlayer child(bridge, gate, &player, &controlChannel, grant, file.fileName());
  QVERIFY(child.grant(&player, FixtureRestart::encodeGrant(grant), 10, true));
  QVERIFY(child.load());
  QByteArray ack;
  QVERIFY(child.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 4}), ack));
  QCOMPARE(stops, 1); QCOMPARE(loads, 1);
  QVERIFY(!child.seek() && !child.load());
#endif
}

void TestRestartChildPlayer::reentrantCancel()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  QCOMPARE(file.write("fixture", 7), qint64(7)); QVERIFY(file.flush());
  PlayerComponent player;
  player.m_mpvGeneration = 8;
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  int controlChannel, stops = 0;
  const auto grant = grantFor("fixture");
  FixtureRestart::RestartChildPlayer child(bridge, gate, &player, &controlChannel, grant, file.fileName());
  QByteArray ack;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  player.m_restoreDispatch = [&](quint64, const char**) {
    child.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 4}), ack);
    return 0;
  };
  QVERIFY(child.grant(&player, FixtureRestart::encodeGrant(grant), 10, true));
  QVERIFY(!child.load());
  QVERIFY(!ack.isEmpty()); QCOMPARE(stops, 1); QVERIFY(!child.seek());
#endif
}

void TestRestartChildPlayer::staleCancel()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open()); QCOMPARE(file.write("fixture", 7), qint64(7)); QVERIFY(file.flush());
  PlayerComponent player;
  player.m_mpvGeneration = 8;
  int stops = 0;
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  int controlChannel;
  const auto grant = grantFor("fixture");
  FixtureRestart::RestartChildPlayer child(bridge, gate, &player, &controlChannel, grant, file.fileName());
  QVERIFY(child.grant(&player, FixtureRestart::encodeGrant(grant), 10, true));
  QVERIFY(child.load());
  QVERIFY(player.load(QStringLiteral("newer"), {{"autoplay", true}}, {}));
  const auto revision = player.m_playbackIntent.snapshot().revision;
  const int before = stops;
  QByteArray ack;
  QVERIFY(!child.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 4}), ack));
  QCOMPARE(stops, before); QCOMPARE(player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(!child.seek() && ack.isEmpty());
#endif
}

void TestRestartChildPlayer::systemUnavailable()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter bridge(player, gate, 7, 2);
  int controlChannel;
  const auto grant = grantFor("fixture");
  FixtureRestart::RestartChildPlayer child(bridge, gate, &player, &controlChannel, grant, {});
  QVERIFY(!child.grant(&player, FixtureRestart::encodeGrant(grant), 10, true));
  QVERIFY(!child.load() && !child.seek());
#endif
}

QTEST_GUILESS_MAIN(TestRestartChildPlayer)
#include "test_restart_child_player.moc"
