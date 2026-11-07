#include "player/PlayerComponent.h"
#include "healthy_restore_claim_test.h"
#include "RestartBroker.h"
#include "RestartWire.h"
#include "frame_capture.h"
#include "capture_capability.h"
#include "restore_test_fixture.h"
#include <QCryptographicHash>
#include <QPainter>
#include <QThread>
#include <QtTest/QtTest>
#include <limits>

class TestHealthyUnpausePlayer : public QObject
{
  Q_OBJECT
  static std::shared_ptr<FixtureRestart::ChildGate> gate(const FixtureRestart::Grant& grant, bool permit);
  static void observe(PlayerComponent& player, quint64 id, bool paused);
private slots:
  void missingPermitAndImage_data();
  void missingPermitAndImage();
  void actualPausedHoldAndUnpause();
};

void TestHealthyUnpausePlayer::observe(PlayerComponent& player, quint64 id, bool paused)
{
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, id, 10000, paused));
  QByteArray text = paused ? "NR1|1|42|yes|yes|3.000|yes|no|no" : "NR1|1|42|yes|yes|3.000|no|no|no";
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 62) | id; event.data = &command;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeObservation.valid);
  QCOMPARE(player.m_nativeObservation.paused, paused);
}

void TestHealthyUnpausePlayer::actualPausedHoldAndUnpause()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize()); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42));
  auto& player = *fixture.player();
  auto child = fixture.gate(); QVERIFY(child);
  QCOMPARE(player.m_restoreGate, child);
  QCOMPARE(player.m_restoreCleanupGate, child);
  QVERIFY(fixture.observe(1, 42, "0.000"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek());
  fixture.replySeekAndRestart();
  QVERIFY(fixture.publishTarget()); QVERIFY(player.healthyCapturePausedHold());
  for (quint64 id = 2; id <= 18; ++id) {
    observe(player, id, true); QVERIFY(player.healthyObservePausedHold());
    if (id != 18) QThread::msleep(200);
  }
  QVERIFY(player.healthyFinishPausedHold());
  QVERIFY(player.nativeRestoreUnpauseReady());
  QVERIFY(player.dispatchNativeRestoreUnpause());
  const auto attempt = player.m_nativeRestore.key.attempt;
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 56) | (attempt + 1);
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
  event = {}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 56) | attempt;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
  observe(player, 19, false);
  QVERIFY(player.nativeRestoreUnpauseComplete());
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
}

std::shared_ptr<FixtureRestart::ChildGate> TestHealthyUnpausePlayer::gate(const FixtureRestart::Grant& grant, bool permit)
{
  static int channel;
  const auto frame = FixtureRestart::encodeGrant(grant);
  const auto digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  auto result = std::make_shared<FixtureRestart::ChildGate>(&channel, 100000);
  if (!result->accept(&channel, frame, grant, digest, 1, true)) return {};
  if (permit) {
    FixtureRestart::RestorePermit value{1, 5, 3, 4, 1, digest};
    if (!result->installPermit(FixtureRestart::encodeRestorePermit(value), value, 1)) return {};
  }
  return result;
}

void TestHealthyUnpausePlayer::missingPermitAndImage_data()
{
  QTest::addColumn<bool>("permit");
  QTest::newRow("missing-permit") << false;
  QTest::newRow("missing-image") << true;
}

void TestHealthyUnpausePlayer::missingPermitAndImage()
{
  QFETCH(bool, permit);
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize()); QVERIFY(fixture.acceptGrant());
  auto& player = *fixture.player();
  auto other = gate(fixture.grant(), true); QVERIFY(other);
  QVERIFY(!player.healthyBindRestoreGate(other, 1, 5, 3, 4, fixture.digest(), [] { return qint64{1}; }));
  if (permit) { QVERIFY(fixture.installPermit()); QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); }
  QCOMPARE(bool(player.m_restoreGate), permit);
  QVERIFY(!player.nativeRestoreUnpauseReady());
  QVERIFY(!player.dispatchNativeRestoreUnpause());
  QVERIFY(!player.nativeRestoreUnpauseReady());
  QVERIFY(!player.dispatchNativeRestoreUnpause());
  QVERIFY(!player.nativeRestoreUnpauseComplete());
}

QTEST_GUILESS_MAIN(TestHealthyUnpausePlayer)
#include "test_healthy_unpause_player.moc"
