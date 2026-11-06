#include "player/PlayerComponent.h"
#include <QtTest/QtTest>

class TestHealthyAsyncStop : public QObject
{
  Q_OBJECT
  using Stop = HealthyRestart::AsyncStop;
  static void prepare(PlayerComponent& player, bool active, bool paused);
  static void reply(PlayerComponent& player, quint64 tag, int error = 0);
  static void property(PlayerComponent& player, quint64 tag, const char* name, mpv_format format, void* data, int error = 0);
  static bool end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason);
private slots:
  void activeOrders();
  void noEntryRequiresFreshPreflight();
  void errorsDuplicatesAndRevocation();
  void reentrantSubmission();
  void queuedDisplayAndUnexpectedEvents();
};

void TestHealthyAsyncStop::prepare(PlayerComponent& player, bool active, bool paused)
{
  QVERIFY(player.bindNativeReceiptHost(7));
  player.m_mpvGeneration = 8;
  player.m_playbackIntent.play(paused);
  player.m_healthyStopClock = [] { return qint64{1}; };
  if (active) {
    const auto intent = player.m_playbackIntent.snapshot();
    const PlaybackNativeReceipts::Key key{7, 8, intent.revision, 2};
    QVERIFY(player.m_nativeReceipts.begin(key));
    QVERIFY(player.m_nativeReceipts.consume(key));
    player.m_nativeReceipts.submitted(0);
    player.m_nativeReceipts.reply(2, true, 42);
    player.m_nativeReceipts.start(42);
  }
}

void TestHealthyAsyncStop::reply(PlayerComponent& player, quint64 tag, int error)
{
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY; event.reply_userdata = tag | 2; event.error = error;
  player.observeHealthyReplacementStop(&event);
}

void TestHealthyAsyncStop::property(PlayerComponent& player, quint64 tag, const char* name,
                                    mpv_format format, void* data, int error)
{
  mpv_event_property value{}; value.name = name; value.format = format; value.data = data;
  mpv_event event{}; event.event_id = MPV_EVENT_GET_PROPERTY_REPLY; event.reply_userdata = tag | 2;
  event.error = error; event.data = &value;
  player.observeHealthyReplacementStop(&event);
}

bool TestHealthyAsyncStop::end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason)
{
  mpv_event_end_file value{}; value.playlist_entry_id = entry; value.reason = reason;
  mpv_event event{}; event.event_id = MPV_EVENT_END_FILE; event.data = &value;
  return player.observeHealthyReplacementStop(&event);
}

void TestHealthyAsyncStop::activeOrders()
{
  for (bool endFirst : {false, true}) {
    PlayerComponent player; prepare(player, true, true);
    int stop = 0, idleQuery = 0; bool commandValid = true;
    player.m_healthyStopDispatch = [&](quint64 id, const char** args, bool propertyCall) {
      if (propertyCall) { ++idleQuery; commandValid &= QByteArray(args[0]) == "idle-active"; }
      else { ++stop; commandValid &= id == ((quint64{1} << 60) | 2) && QByteArray(args[0]) == "stop"; }
      return 0;
    };
    QVERIFY(player.beginHealthyReplacementStop(7, 2, 100));
    if (endFirst) QVERIFY(end(player, 42, MPV_END_FILE_REASON_STOP));
    reply(player, quint64{1} << 60);
    if (!endFirst) QVERIFY(end(player, 42, MPV_END_FILE_REASON_STOP));
    QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Pending);
    int idle = 1; property(player, quint64{1} << 57, "idle-active", MPV_FORMAT_FLAG, &idle);
    QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Complete);
    QCOMPARE(stop, 1); QCOMPARE(idleQuery, 1); QVERIFY(commandValid);
    QCOMPARE(player.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Paused);
  }
}

void TestHealthyAsyncStop::noEntryRequiresFreshPreflight()
{
  PlayerComponent player; prepare(player, false, false);
  int commands = 0, queries = 0;
  player.m_healthyStopDispatch = [&](quint64, const char**, bool query) { query ? ++queries : ++commands; return 0; };
  QVERIFY(player.beginHealthyReplacementStop(7, 2, 100));
  QCOMPARE(queries, 2); QCOMPARE(commands, 0);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 58, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Pending);
  QCOMPARE(commands, 0);
  property(player, quint64{1} << 59, "idle-active", MPV_FORMAT_FLAG, &idle);
  player.pollHealthyReplacementStop();
  QCOMPARE(commands, 1);
  reply(player, quint64{1} << 60);
  player.pollHealthyReplacementStop();
  property(player, quint64{1} << 57, "idle-active", MPV_FORMAT_FLAG, &idle);
  QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Complete);
}

void TestHealthyAsyncStop::errorsDuplicatesAndRevocation()
{
  PlayerComponent player; prepare(player, true, false);
  player.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(player.beginHealthyReplacementStop(7, 2, 100));
  reply(player, quint64{1} << 60, -17);
  QCOMPARE(player.m_healthyStop.facts().replyError, -17);
  QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Failed);
  PlayerComponent wrong; prepare(wrong, true, false);
  wrong.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(wrong.beginHealthyReplacementStop(7, 2, 100));
  mpv_event stray{}; stray.event_id = MPV_EVENT_COMMAND_REPLY;
  stray.reply_userdata = (quint64{1} << 60) | 3;
  wrong.observeHealthyReplacementStop(&stray);
  QVERIFY(!wrong.m_healthyStop.facts().replied);
  reply(wrong, quint64{1} << 60);
  reply(wrong, quint64{1} << 60);
  QCOMPARE(wrong.pollHealthyReplacementStop(), Stop::Status::Failed);
  PlayerComponent superseded; prepare(superseded, true, false);
  superseded.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(superseded.beginHealthyReplacementStop(7, 2, 100));
  superseded.m_playbackIntent.play(false);
  reply(superseded, quint64{1} << 60);
  QCOMPARE(superseded.pollHealthyReplacementStop(), Stop::Status::Failed);
  PlayerComponent overflow; prepare(overflow, true, false);
  overflow.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(overflow.beginHealthyReplacementStop(7, 2, 100));
  mpv_event event{}; event.event_id = MPV_EVENT_QUEUE_OVERFLOW;
  overflow.observeHealthyReplacementStop(&event);
  QCOMPARE(overflow.pollHealthyReplacementStop(), Stop::Status::Failed);
  PlayerComponent unexpected; prepare(unexpected, false, false);
  unexpected.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  QVERIFY(unexpected.beginHealthyReplacementStop(7, 2, 100));
  mpv_event_start_file start{}; start.playlist_entry_id = 55;
  event.event_id = MPV_EVENT_START_FILE; event.data = &start;
  unexpected.observeHealthyReplacementStop(&event);
  QCOMPARE(unexpected.pollHealthyReplacementStop(), Stop::Status::Failed);
}

void TestHealthyAsyncStop::reentrantSubmission()
{
  PlayerComponent player; prepare(player, true, true);
  player.m_healthyStopDispatch = [&](quint64, const char**, bool query) {
    if (!query) reply(player, quint64{1} << 60);
    return -9;
  };
  QVERIFY(!player.beginHealthyReplacementStop(7, 2, 100));
  const auto facts = player.m_healthyStop.facts();
  QVERIFY(facts.stopRequested && facts.submissionKnown && !facts.submitted);
  QCOMPARE(facts.submissionError, -9);
  QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Failed);
}

void TestHealthyAsyncStop::queuedDisplayAndUnexpectedEvents()
{
  for (const auto kind : {MPV_EVENT_HOOK, MPV_EVENT_CLIENT_MESSAGE}) {
    PlayerComponent player; prepare(player, true, false);
    int displays = 0;
    player.m_restoreDisplayAction = [&] { ++displays; };
    player.onRestoreDisplay();
    QCOMPARE(displays, 1);
    player.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
    QVERIFY(player.beginHealthyReplacementStop(7, 2, 100));
    player.onRestoreDisplay();
    QCOMPARE(displays, 1);
    mpv_event event{}; event.event_id = kind;
    player.observeHealthyReplacementStop(&event);
    QCOMPARE(player.pollHealthyReplacementStop(), Stop::Status::Failed);
  }
}

QTEST_GUILESS_MAIN(TestHealthyAsyncStop)
#include "test_healthy_async_stop_player.moc"
