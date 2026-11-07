#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <RestartWire.h>
#include <restore_test_fixture.h>
#endif

class TestPlayerRestoreObservation : public QObject
{
  Q_OBJECT
#ifndef HEALTHY_COPIED_SOURCE_TEST
  PlayerTestCache cache_;
#endif
  static void reply(PlayerComponent& player, quint64 id, const QByteArray& value);
  static void seekReply(PlayerComponent& player, int error = 0);
  static void restart(PlayerComponent& player);
  static void prepare(PlayerComponent& player);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  static void diagnosticReplacement(int mode);
#endif
private slots:
  void initTestCase();
  void barrierAndStaleReply();
  void failedSeekAndReplacement();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  void diagnosticStopReplacement();
  void diagnosticLoadReplacement();
  void diagnosticCoreReplacement();
#endif
  void ordinaryObservation();
  void unavailableWithoutExternalEvents();
};

void TestPlayerRestoreObservation::initTestCase()
{
#ifdef HEALTHY_COPIED_SOURCE_TEST
  HealthyRestoreTestFixture::initializeProcessCache();
#endif
}

void TestPlayerRestoreObservation::reply(PlayerComponent& player, quint64 id, const QByteArray& value)
{
  mpv_event_command command{};
  command.result.format = MPV_FORMAT_STRING;
  command.result.u.string = const_cast<char*>(value.constData());
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 62) | id;
  event.data = &command;
  player.observeNativeReceipt(&event);
}

void TestPlayerRestoreObservation::seekReply(PlayerComponent& player, int error)
{
  mpv_event event{};
  event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 61) | player.m_nativeReceipts.snapshot().key.attempt;
  event.error = error;
  player.observeNativeReceipt(&event);
}

void TestPlayerRestoreObservation::restart(PlayerComponent& player)
{
  mpv_event event{};
  event.event_id = MPV_EVENT_PLAYBACK_RESTART;
  player.observeNativeReceipt(&event);
}

void TestPlayerRestoreObservation::prepare(PlayerComponent& player)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3125, true};
  QVERIFY(player.armNativeRestore(grant, 7, 2));
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
  player.m_nativeReceipts.reply(2, true, 42);
  player.m_nativeReceipts.start(42);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.requestNativeObservation(key, 1, 10000, true));
  reply(player, 1, "NR1|1|42|yes|yes|0|yes|no|no");
  QVERIFY(player.nativeObservationCurrent());
  QVERIFY(!player.nativePostSeekObservationCurrent());
#else
  Q_UNUSED(player);
#endif
}

void TestPlayerRestoreObservation::barrierAndStaleReply()
{
#ifdef MPVQT_EXTERNAL_EVENTS
#ifdef HEALTHY_COPIED_SOURCE_TEST
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(true, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QCOMPARE(fixture.state()->loads, 1u); QVERIFY(fixture.state()->pausedLoad);
  QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget());
  auto& player = *fixture.player();
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(fixture.seek()); QCOMPARE(fixture.state()->seeks, 1u);
  QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  restart(player); QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  seekReply(player); QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  restart(player);
  reply(player, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 3, 10000, true));
  reply(player, 1, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!player.nativePostSeekObservationCurrent());
  reply(player, 3, "NR1|1|42|yes|yes|3.101|yes|no|no");
  QVERIFY(player.nativeObservationCurrent()); QVERIFY(!player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 4, 10000, true));
  reply(player, 4, "NR1|1|42|yes|yes|3.100|yes|no|no");
  QVERIFY(player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 5, 10000, false));
  reply(player, 5, "NR1|1|42|yes|yes|3|no|no|no");
  QVERIFY(player.nativeObservationCurrent()); QVERIFY(!player.nativePostSeekObservationCurrent());
#else
  PlayerComponent player;
  prepare(player);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.dispatchNativeRestoreSeek());
  QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  restart(player);
  QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  seekReply(player);
  QVERIFY(!player.requestNativeObservation(key, 3, 10000, true));
  restart(player);
  reply(player, 1, "NR1|1|42|yes|yes|3.125|yes|no|no");
  QVERIFY(!player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 3, 10000, true));
  reply(player, 1, "NR1|1|42|yes|yes|3.125|yes|no|no");
  QVERIFY(!player.nativePostSeekObservationCurrent());
  reply(player, 3, "NR1|1|42|yes|yes|3.226|yes|no|no");
  QVERIFY(player.nativeObservationCurrent());
  QVERIFY(!player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 4, 10000, true));
  reply(player, 4, "NR1|1|42|yes|yes|3.225|yes|no|no");
  QVERIFY(player.nativePostSeekObservationCurrent());
  QVERIFY(player.requestNativeObservation(key, 5, 10000, false));
  reply(player, 5, "NR1|1|42|yes|yes|3.125|no|no|no");
  QVERIFY(player.nativeObservationCurrent());
  QVERIFY(!player.nativePostSeekObservationCurrent());
#endif
#endif
}

void TestPlayerRestoreObservation::failedSeekAndReplacement()
{
#ifdef MPVQT_EXTERNAL_EVENTS
#ifdef HEALTHY_COPIED_SOURCE_TEST
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(true, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek());
  QCOMPARE(fixture.state()->loads, 1u); QCOMPARE(fixture.state()->seeks, 1u);
  auto& player = *fixture.player(); const auto key = player.m_nativeReceipts.snapshot().key;
  seekReply(player, -1); restart(player);
  QVERIFY(!player.requestNativeObservation(key, 2, 10000, true));
  QVERIFY(!player.nativePostSeekObservationCurrent());
#else
  PlayerComponent player;
  prepare(player);
  const auto key = player.m_nativeReceipts.snapshot().key;
  QVERIFY(player.dispatchNativeRestoreSeek());
  seekReply(player, -1);
  restart(player);
  QVERIFY(!player.requestNativeObservation(key, 2, 10000, true));
  QVERIFY(!player.nativePostSeekObservationCurrent());
  PlayerComponent replaced;
  prepare(replaced);
  const auto old = replaced.m_nativeReceipts.snapshot().key;
  QVERIFY(replaced.dispatchNativeRestoreSeek());
  seekReply(replaced);
  restart(replaced);
  QVERIFY(replaced.requestNativeObservation(old, 2, 10000, true));
  replaced.requestNativeStop();
  reply(replaced, 2, "NR1|1|42|yes|yes|3.125|yes|no|no");
  QVERIFY(!replaced.nativePostSeekObservationCurrent());
  PlayerComponent loaded;
  prepare(loaded);
  const auto previous = loaded.m_nativeReceipts.snapshot().key;
  QVERIFY(loaded.dispatchNativeRestoreSeek());
  seekReply(loaded);
  restart(loaded);
  QVERIFY(loaded.requestNativeObservation(previous, 2, 10000, true));
  loaded.load(QStringLiteral("replacement"), {}, {});
  reply(loaded, 2, "NR1|1|42|yes|yes|3.125|yes|no|no");
  QVERIFY(!loaded.nativePostSeekObservationCurrent());
#endif
#endif
}

#ifdef HEALTHY_COPIED_SOURCE_TEST
void TestPlayerRestoreObservation::diagnosticReplacement(int mode)
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(fixture.initialize(true, 3000)); QVERIFY(fixture.acceptGrantAndPermit());
  QVERIFY(fixture.load()); QVERIFY(fixture.replyLoad(42)); QVERIFY(fixture.observe(1, 42, "0"));
  QVERIFY(fixture.publishBaseline()); QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek());
  QCOMPARE(fixture.state()->loads, 1u); QCOMPARE(fixture.state()->seeks, 1u);
  auto& player = *fixture.player(); const auto key = player.m_nativeReceipts.snapshot().key;
  seekReply(player); restart(player);
  QVERIFY(player.requestNativeObservation(key, 2, 10000, true));
  if (mode == 0) player.requestNativeStop();
  else if (mode == 1) player.load(QStringLiteral("replacement"), {}, {});
  else ++player.m_mpvGeneration;
  const auto generation = player.m_mpvGeneration;
  reply(player, 2, "NR1|1|42|yes|yes|3|yes|no|no");
  QVERIFY(!player.nativePostSeekObservationCurrent());
  if (mode == 2) {
    const FixtureRestart::CancelResult expected{fixture.grant().run, 5, fixture.grant().grant,
        fixture.grant().revision, 2, key.host, key.core, key.revision, key.attempt,
        FixtureRestart::CancelOutcome::Failed, FixtureRestart::CancelFailure::CleanupUnconfirmed};
    const auto stops = fixture.state()->stops;
    QByteArray response; QVERIFY(fixture.cancel(2, 5, response));
    QCOMPARE(response, FixtureRestart::encodeCancelResult(expected));
    QCOMPARE(player.m_mpvGeneration, generation); QCOMPARE(fixture.state()->stops, stops);
    QCOMPARE(stops, 0u); QVERIFY(!player.nativePostSeekObservationCurrent());
  }
}
void TestPlayerRestoreObservation::diagnosticStopReplacement() { diagnosticReplacement(0); }
void TestPlayerRestoreObservation::diagnosticLoadReplacement() { diagnosticReplacement(1); }
void TestPlayerRestoreObservation::diagnosticCoreReplacement() { diagnosticReplacement(2); }
#endif

void TestPlayerRestoreObservation::ordinaryObservation()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  player.m_playbackIntent.play(false);
  const PlaybackNativeReceipts::Key key{7, 8, player.m_playbackIntent.snapshot().revision, 2};
  QVERIFY(player.m_nativeReceipts.begin(key));
  QVERIFY(player.m_nativeReceipts.consume(key));
  player.m_nativeReceipts.submitted(0);
  player.m_nativeReceipts.reply(2, true, 42);
  player.m_nativeReceipts.start(42);
  player.m_observationDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.requestNativeObservation(key, 1, 10000, false));
  reply(player, 1, "NR1|1|42|yes|yes|2.000|no|no|no");
  QVERIFY(player.nativeObservationCurrent());
  QVERIFY(!player.nativePostSeekObservationCurrent());
#endif
}

void TestPlayerRestoreObservation::unavailableWithoutExternalEvents()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(!player.requestNativeObservation({}, 1, 10000, true));
  QVERIFY(!player.nativePostSeekObservationCurrent());
#endif
}

QTEST_GUILESS_MAIN(TestPlayerRestoreObservation)
#include "test_player_restore_observation.moc"
