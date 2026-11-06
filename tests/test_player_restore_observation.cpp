#include "player/PlayerComponent.h"
#include <QtTest/QtTest>

class TestPlayerRestoreObservation : public QObject
{
  Q_OBJECT
  static void reply(PlayerComponent& player, quint64 id, const QByteArray& value);
  static void seekReply(PlayerComponent& player, int error = 0);
  static void restart(PlayerComponent& player);
  static void prepare(PlayerComponent& player);
private slots:
  void barrierAndStaleReply();
  void failedSeekAndReplacement();
  void ordinaryObservation();
  void unavailableWithoutExternalEvents();
};

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
  event.reply_userdata = (quint64{1} << 61) | 2;
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
}

void TestPlayerRestoreObservation::failedSeekAndReplacement()
{
#ifdef MPVQT_EXTERNAL_EVENTS
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
}

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
