#include "fixture_restart/FixtureRestartAdapter.h"
#include "fixture_restart/OwnedFixtureFile.h"
#include "fixture_restart/RestartSourcePlayer.h"
#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QCryptographicHash>
#include <QTemporaryFile>
#include <QtTest/QtTest>

// Reuse the existing fixture-only Player friendship in a separate executable.
class TestRestartChildPlayer : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private slots:
  void freezesNativeEvidenceBeforeInternalStop();
  void staleEvidenceNeverStopsNewIntent();
};

void TestRestartChildPlayer::freezesNativeEvidenceBeforeInternalStop()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  const QByteArray bytes("fixture-a");
  QCOMPARE(file.write(bytes), qint64(bytes.size())); QVERIFY(file.flush());
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  PlayerComponent player;
  player.m_mpvGeneration = 3;
  player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  int stopCount = 0;
  player.m_stopDispatch = [&] { ++stopCount; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter adapter(player, gate, 4, 1);
  QVERIFY(adapter.loadA(file.fileName(), hash, false));
  player.m_nativeReceipts.reply(1, true, 42);
  player.m_nativeReceipts.start(42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 7, 10000, false));
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_STRING;
  QByteArray observation("NR1|1|42|yes|yes|3|no|no|no");
  command.result.u.string = observation.data();
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | 7;
  event.data = &command;
  player.observeNativeReceipt(&event);
  FixtureRestart::Grant binding{1, 2, 3, hash, 0, false};
  int evidenceChannel, controlChannel;
  FixtureRestart::RestartSourcePlayer source(adapter, &evidenceChannel, &controlChannel, binding);
  QByteArray frame, ack;
  QVERIFY(!source.freeze(&controlChannel, frame));
  QVERIFY(source.freeze(&evidenceChannel, frame));
  FixtureRestart::Evidence decoded;
  QVERIFY(FixtureRestart::decodeEvidence(frame, binding, true, decoded));
  QCOMPARE(decoded.grant.positionMs, qint64(3000));
  const auto revision = player.m_playbackIntent.snapshot().revision;
  QVERIFY(source.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::InternalStop, 1, 3}), ack));
  QCOMPARE(stopCount, 1); QCOMPARE(player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(player.m_nativeReceipts.snapshot().invalid);
  QVERIFY(!source.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::InternalStop, 1, 3}), ack));
#else
  QVERIFY(!ownedFixtureTemplate().isEmpty());
  QTemporaryFile file(ownedFixtureTemplate());
  QVERIFY(file.open());
  const QByteArray bytes("fixture-a");
  QCOMPARE(file.write(bytes), qint64(bytes.size())); QVERIFY(file.flush());
  const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
  PlayerComponent player;
  int calls = 0;
  player.m_receiptDispatch = [&](quint64, const QString&) { ++calls; return 0; };
  FixtureRestart::ChildGate gate(&player, 100);
  FixtureRestartAdapter adapter(player, gate, 4, 1);
  QVERIFY(!adapter.loadA(file.fileName(), hash, false));
  FixtureRestart::Evidence evidence;
  QVERIFY(!adapter.captureA({1, 2, 3, hash, 0, false}, evidence));
  QCOMPARE(calls, 0);
#endif
}

void TestRestartChildPlayer::staleEvidenceNeverStopsNewIntent()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  for (bool newerLoad : {false, true}) {
    QVERIFY(!ownedFixtureTemplate().isEmpty());
    QTemporaryFile file(ownedFixtureTemplate());
    QVERIFY(file.open());
    const QByteArray bytes("fixture-a");
    QCOMPARE(file.write(bytes), qint64(bytes.size())); QVERIFY(file.flush());
    const auto hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
    PlayerComponent player;
    player.m_mpvGeneration = 3;
    player.m_receiptDispatch = [](quint64, const QString&) { return 0; };
    player.m_observationDispatch = [](quint64, const char**) { return 0; };
    int stops = 0;
    player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
    FixtureRestart::ChildGate gate(&player, 100);
    FixtureRestartAdapter adapter(player, gate, 4, 1);
    QVERIFY(adapter.loadA(file.fileName(), hash, false));
    player.m_nativeReceipts.reply(1, true, 42); player.m_nativeReceipts.start(42);
    QVERIFY(player.requestNativeObservation(player.m_nativeReceipts.snapshot().key, 7, 10000, false));
    mpv_event_command command{}; command.result.format = MPV_FORMAT_STRING;
    QByteArray observation("NR1|1|42|yes|yes|3|no|no|no"); command.result.u.string = observation.data();
    mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
    event.reply_userdata = (quint64{1} << 62) | 7; event.data = &command;
    player.observeNativeReceipt(&event);
    int evidenceChannel, controlChannel;
    FixtureRestart::RestartSourcePlayer source(adapter, &evidenceChannel, &controlChannel, {1, 2, 3, hash, 0, false});
    QByteArray frame, ack;
    QVERIFY(source.freeze(&evidenceChannel, frame));
    if (newerLoad) QVERIFY(player.load(QStringLiteral("newer"), {{"autoplay", true}}, {}));
    else player.requestNativeStop();
    const auto revision = player.m_playbackIntent.snapshot().revision;
    const int before = stops;
    QVERIFY(!source.control(&controlChannel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::InternalStop, 1, 3}), ack));
    QCOMPARE(stops, before); QCOMPARE(player.m_playbackIntent.snapshot().revision, revision); QVERIFY(ack.isEmpty());
  }
#endif
}

QTEST_GUILESS_MAIN(TestRestartChildPlayer)
#include "test_restart_source_player.moc"
