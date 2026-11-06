#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include "RestartBroker.h"
#include "RestartWire.h"
#include "frame_capture.h"
#include <QCryptographicHash>
#include <QPainter>
#include <QThread>
#include <QtTest/QtTest>
#include <limits>

class TestHealthyUnpausePlayer : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
  static void prepare(PlayerComponent& player, bool paused);
  static std::shared_ptr<FixtureRestart::ChildGate> gate(bool permit);
  static void observe(PlayerComponent& player, quint64 id, bool paused);
  static void image(const fixture_restart::CaptureTicket& ticket, int index);
private slots:
  void missingPermitAndImage();
  void actualPausedHoldAndUnpause();
};

void TestHealthyUnpausePlayer::prepare(PlayerComponent& player, bool paused)
{
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, paused};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  player.m_nativeReceipts.reply(2, true, 42);
  player.m_nativeReceipts.start(42);
  QVERIFY(player.healthyInstallRetirementClaim(std::make_shared<HealthyRetirement::RetirementClaim>(9), 9));
}

void TestHealthyUnpausePlayer::observe(PlayerComponent& player, quint64 id, bool paused)
{
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, id, 10000, true));
  QByteArray text = paused ? "NR1|1|42|yes|yes|3.000|yes|no|no" : "NR1|1|42|yes|yes|3.000|no|no|no";
  mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING; command.result.u.string = text.data();
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 62) | id; event.data = &command;
  player.observeNativeReceipt(&event);
}

void TestHealthyUnpausePlayer::image(const fixture_restart::CaptureTicket& ticket, int index)
{
  QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
  QPainter painter(&image);
  for (int bit = 0; bit < 9; ++bit) {
    const int value = index & (1 << bit) ? 220 : 70;
    painter.fillRect(5 + bit * 16, 8, 12, 12, QColor(value, value, value));
  }
  painter.fillRect((index * 3) % 140, 35, 20, 20, QColor(220, 220, 220));
  painter.end();
  fixture_restart::publishCapture(ticket, image, 0, 0, 0, 0, ticket.key.context,
                                  fixture_restart::nextRenderSerial());
}

void TestHealthyUnpausePlayer::actualPausedHoldAndUnpause()
{
  PlayerComponent player; prepare(player, false);
  auto child = gate(true); QVERIFY(child);
  FixtureRestart::Grant grant{1, 3, 4, QByteArray(64, 'a'), 3000, false};
  const auto digest = QCryptographicHash::hash(FixtureRestart::encodeGrant(grant), QCryptographicHash::Sha256);
  QVERIFY(player.healthyBindRestoreGate(child, 1, 5, 3, 4, digest, [] { return qint64{1}; }));
  observe(player, 1, true);
  fixture_restart::FrameKey key{11, 8, 12, 42, 1, 1};
  fixture_restart::armCapture(key, 13);
  const auto ticket = fixture_restart::captureTicket(13); QVERIFY(ticket.generation);
  image(ticket, 0);
  HealthyRestart::PausedHoldBinding binding;
  binding.frame = key; binding.host = 7; binding.revision = player.m_nativeRestore.key.revision;
  binding.attempt = 2; binding.grant = 3; binding.checkpointMs = 3000;
  binding.deadlineNs = std::numeric_limits<qint64>::max(); binding.claim = player.healthyStopClaim();
  QVERIFY(player.healthyBeginPausedHold(binding));
  QVERIFY(player.dispatchNativeRestoreSeek());
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 61) | 2;
  player.observeNativeReceipt(&event);
  event = {}; event.event_id = MPV_EVENT_PLAYBACK_RESTART; player.observeNativeReceipt(&event);
  image(ticket, 90); QVERIFY(player.healthyCapturePausedHold());
  for (quint64 id = 2; id <= 18; ++id) {
    observe(player, id, true); QVERIFY(player.healthyObservePausedHold());
    if (id != 18) QThread::msleep(200);
  }
  QVERIFY(player.healthyFinishPausedHold());
  QVERIFY(player.nativeRestoreUnpauseReady());
  QVERIFY(player.dispatchNativeRestoreUnpause());
  event = {}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 56) | 3;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
  event = {}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = (quint64{1} << 56) | 2;
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
  observe(player, 19, false);
  QVERIFY(player.nativeRestoreUnpauseComplete());
  player.observeNativeReceipt(&event);
  QVERIFY(!player.nativeRestoreUnpauseComplete());
  fixture_restart::disarmCapture();
}

std::shared_ptr<FixtureRestart::ChildGate> TestHealthyUnpausePlayer::gate(bool permit)
{
  static int channel;
  FixtureRestart::Grant grant{1, 3, 4, QByteArray(64, 'a'), 3000, false};
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

void TestHealthyUnpausePlayer::missingPermitAndImage()
{
  for (bool permit : {false, true}) {
    PlayerComponent player; prepare(player, false);
    auto child = gate(permit); QVERIFY(child);
    FixtureRestart::Grant grant{1, 3, 4, QByteArray(64, 'a'), 3000, false};
    const auto digest = QCryptographicHash::hash(FixtureRestart::encodeGrant(grant), QCryptographicHash::Sha256);
    const bool bound = player.healthyBindRestoreGate(child, 1, 5, 3, 4, digest, [] { return qint64{1}; });
    QCOMPARE(bound, permit);
    QVERIFY(!player.nativeRestoreUnpauseReady());
    QVERIFY(!player.dispatchNativeRestoreUnpause());
    QVERIFY(!player.nativeRestoreUnpauseReady());
    QVERIFY(!player.dispatchNativeRestoreUnpause());
    QVERIFY(!player.nativeRestoreUnpauseComplete());
  }
}

QTEST_GUILESS_MAIN(TestHealthyUnpausePlayer)
#include "test_healthy_unpause_player.moc"
