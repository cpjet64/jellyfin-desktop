#include "player/PlayerComponent.h"
#ifndef HEALTHY_RETIREMENT_TESTING
#include "player_test_cache.h"
#endif
#include <RestartBroker.h>
#include <RestartChild.h>
#include <QCryptographicHash>
#include <QtTest/QtTest>
#include <QScopeGuard>
#include <map>
#ifdef HEALTHY_RETIREMENT_TESTING
#include "restore_test_fixture.h"
#endif
class HealthyRestoreTestFixture;

class TestHealthyRestoreCleanup : public QObject
{
  Q_OBJECT
#ifndef HEALTHY_RETIREMENT_TESTING
  PlayerTestCache cache_;
#endif
  using Gate = FixtureRestart::ChildGate;
  using Status = PlayerComponent::RestoreCleanupStatus;
  static bool checked(void*) { return true; }
  static void clearCallbacks(PlayerComponent& player, HealthyRestoreTestFixture& fixture);
  std::shared_ptr<Gate> prepare(HealthyRestoreTestFixture& fixture, int& stops, bool load);
  static void event(PlayerComponent& player, mpv_event& value);
  static void loadReply(PlayerComponent& player, qint64 entry);
  static void start(PlayerComponent& player, qint64 entry);
  static void reply(PlayerComponent& player, quint64 tag, int error = 0);
  static void end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason);
  static void property(PlayerComponent& player, quint64 tag, const char* name, mpv_format format, void* data);
  bool cancel(HealthyRestoreTestFixture& fixture, const std::shared_ptr<Gate>& gate);
private slots:
  void initTestCase();
  void beforeLoad();
  void noLoadRejectsUnsolicitedEvents();
  void noLoadRejectsUnsolicitedEvents_data();
  void reentrantQueriesRequireLaterDrain();
  void canceledGateCannotBind();
  void submittedWithoutStart();
  void bufferedStartBeforeReply();
  void lateEntryDuringTerminalQuery();
  void lateEntryDuringTerminalQuery_data();
  void terminalOrdering();
  void terminalOrdering_data();
  void pendingSeekBlocksSettlement();
  void rejectedAndWrongReplies();
  void rejectedAndWrongReplies_data();
  void duplicateAndReentrant();
  void supersededAndEventLoss();
  void supersededAndEventLoss_data();
  void missingIdentityAndRedirect();
  void missingIdentityAndRedirect_data();
  void disabledConfiguration();
};

void TestHealthyRestoreCleanup::initTestCase()
{
#ifdef HEALTHY_RETIREMENT_TESTING
  HealthyRestoreTestFixture::initializeProcessCache();
#endif
}
void TestHealthyRestoreCleanup::clearCallbacks(PlayerComponent& player, HealthyRestoreTestFixture& fixture)
{
#ifdef HEALTHY_RETIREMENT_TESTING
  player.m_restoreDispatch = {}; player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {};
  player.m_healthyStopClock = {}; player.m_observationDispatch = {}; player.m_stopDispatch = {};
  auto state = fixture.state(); state->clockHook = {}; state->moduleHook = {}; state->endpointHook = {}; state->submissionHook = {};
#else
  Q_UNUSED(player); Q_UNUSED(fixture);
#endif
}

std::shared_ptr<TestHealthyRestoreCleanup::Gate>
TestHealthyRestoreCleanup::prepare(HealthyRestoreTestFixture& fixture, int& stops, bool load)
{
#if defined(MPVQT_EXTERNAL_EVENTS) && defined(HEALTHY_RETIREMENT_TESTING)
  if (!fixture.initialize(true) || !fixture.acceptGrantAndPermit()) return {};
  auto& player = *fixture.player();
  auto gate = fixture.gate();
  if (!gate || (load && !fixture.load())) return {};
  player.m_restoreDispatch = [&](quint64, const char** args) {
    if (QByteArray(args[0]) == "stop") ++stops;
    return 0;
  };
  player.m_healthyStopDispatch = [](quint64, const char**, bool) { return 0; };
  return gate;
#else
  Q_UNUSED(fixture); Q_UNUSED(stops); Q_UNUSED(load); return {};
#endif
}

void TestHealthyRestoreCleanup::event(PlayerComponent& player, mpv_event& value)
{
  player.observeNativeRestoreCleanup(&value);
  player.observeNativeReceipt(&value);
}

void TestHealthyRestoreCleanup::loadReply(PlayerComponent& player, qint64 entry)
{
  mpv_node node{}; node.format = MPV_FORMAT_INT64; node.u.int64 = entry;
  char name[] = "playlist_entry_id"; char* names[] = {name};
  mpv_node_list list{}; list.num = 1; list.keys = names; list.values = &node;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event value{}; value.event_id = MPV_EVENT_COMMAND_REPLY;
  value.reply_userdata = (quint64{1} << 63) | player.m_nativeRestore.key.attempt; value.data = &command;
  event(player, value);
}

void TestHealthyRestoreCleanup::start(PlayerComponent& player, qint64 entry)
{
  mpv_event_start_file data{}; data.playlist_entry_id = entry;
  mpv_event value{}; value.event_id = MPV_EVENT_START_FILE; value.data = &data; event(player, value);
}

void TestHealthyRestoreCleanup::reply(PlayerComponent& player, quint64 tag, int error)
{
  mpv_event value{}; value.event_id = MPV_EVENT_COMMAND_REPLY;
  value.reply_userdata = tag | player.m_nativeRestore.key.attempt; value.error = error; event(player, value);
}

void TestHealthyRestoreCleanup::end(PlayerComponent& player, qint64 entry, mpv_end_file_reason reason)
{
  mpv_event_end_file data{}; data.playlist_entry_id = entry; data.reason = reason;
  mpv_event value{}; value.event_id = MPV_EVENT_END_FILE; value.data = &data; event(player, value);
}

void TestHealthyRestoreCleanup::property(PlayerComponent& player, quint64 tag, const char* name,
                                         mpv_format format, void* data)
{
  mpv_event_property prop{}; prop.name = name; prop.format = format; prop.data = data;
  mpv_event value{}; value.event_id = MPV_EVENT_GET_PROPERTY_REPLY;
  value.reply_userdata = tag | player.m_nativeRestore.key.attempt; value.data = &prop; event(player, value);
}

bool TestHealthyRestoreCleanup::cancel(HealthyRestoreTestFixture& fixture, const std::shared_ptr<Gate>& gate)
{
#ifdef HEALTHY_RETIREMENT_TESTING
  if (!gate || fixture.gate() != gate) return false;
  QByteArray response;
  return fixture.cancel(11, 5, response) && response.isEmpty();
#else
  Q_UNUSED(fixture); Q_UNUSED(gate); return false;
#endif
}

void TestHealthyRestoreCleanup::beforeLoad()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, false); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  const auto deadline = player.m_restoreCleanup.deadlineNs;
  int channel = 0; auto wrong = std::make_shared<Gate>(&channel, deadline);
  FixtureRestart::RestartChild other(*wrong, &channel, &channel, fixture.grant().run, 5, fixture.digest(), {});
  QByteArray wrongResponse;
  QVERIFY(other.control(&channel, FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 11, 5}), wrongResponse));
  QCOMPARE(wrongResponse, FixtureRestart::encodeCancelNoArm({fixture.grant().run, 5, 5, 11, fixture.digest()}));
  QVERIFY(wrong->matchesVerifiedCancel(11, 5)); QVERIFY(!player.m_restoreCleanup.claimed);
  QVERIFY(!player.beginNativeRestoreCleanup(wrong, HealthyRestart::VerifiedCleanupCancel{11, 5},
                                            deadline, HealthyRestart::FailureCause::None));
  QVERIFY(!player.m_restoreCleanup.claimed);
  QVERIFY(cancel(fixture, gate)); QVERIFY(gate->matchesVerifiedCancel(11, 5));
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(stops, 0);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::NoLoad);
  QCOMPARE(stops, 0); QVERIFY(!player.dispatchNativeRestoreLoad(QStringLiteral("fixture")));
#endif
}

void TestHealthyRestoreCleanup::noLoadRejectsUnsolicitedEvents()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(int, kind);
  {
    HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, false); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
    QVERIFY(cancel(fixture, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (kind == 0) start(player, 42);
    else if (kind == 1) end(player, 42, MPV_END_FILE_REASON_STOP);
    else reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); QCOMPARE(stops, 0);
  }
#endif
}
void TestHealthyRestoreCleanup::noLoadRejectsUnsolicitedEvents_data()
{
  QTest::addColumn<int>("kind"); QTest::newRow("start") << 0;
  QTest::newRow("end") << 1; QTest::newRow("reply") << 2;
}

void TestHealthyRestoreCleanup::canceledGateCannotBind()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; auto gate = std::make_shared<Gate>(&player, 100);
  QVERIFY(gate->cancel(checked, nullptr));
  QVERIFY(!player.healthyBindRestoreCleanupGate(gate, 100));
#endif
}

void TestHealthyRestoreCleanup::reentrantQueriesRequireLaterDrain()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, false); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  mpv_event none{}; none.event_id = MPV_EVENT_NONE; int drains = 0;
  player.m_healthyStopEventWait = [&]() -> mpv_event* { ++drains; return &none; };
  int idle = 1; qint64 position = -1; int queries = 0;
  player.m_healthyStopDispatch = [&](quint64 id, const char**, bool query) {
    if (!query) return -1;
    ++queries;
    if (id == ((quint64{1} << 54) | player.m_nativeRestore.key.attempt))
      property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
    else property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
    return 0;
  };
  QVERIFY(cancel(fixture, gate));
  QCOMPARE(player.m_restoreCleanup.status, Status::Pending);
  QCOMPARE(drains, 1); QCOMPARE(queries, 2); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::NoLoad);
  QCOMPARE(drains, 2); QCOMPARE(stops, 0);
#endif
}

void TestHealthyRestoreCleanup::submittedWithoutStart()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  qint64 now = fixture.state()->clockNs.load(); player.m_healthyStopClock = [&] { return now; };
  QVERIFY(cancel(fixture, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 0);
  loadReply(player, 42);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  now += 50000000000LL; QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

void TestHealthyRestoreCleanup::bufferedStartBeforeReply()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  mpv_event_start_file data{}; data.playlist_entry_id = 42;
  mpv_event started{}; started.event_id = MPV_EVENT_START_FILE; started.data = &data;
  mpv_event none{}; none.event_id = MPV_EVENT_NONE; int reads = 0; bool stopAfterDrain = true;
  player.m_healthyStopEventWait = [&]() -> mpv_event* {
    if (reads < 2 && stops != 0) stopAfterDrain = false;
    return reads++ == 0 ? &started : &none;
  };
  QVERIFY(cancel(fixture, gate)); QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 0); QVERIFY(stopAfterDrain);
  loadReply(player, 42); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  reply(player, quint64{1} << 55);
  end(player, 42, MPV_END_FILE_REASON_STOP);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped);
#endif
}

void TestHealthyRestoreCleanup::lateEntryDuringTerminalQuery()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(bool, starting);
  {
    HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
    loadReply(player, 42); QVERIFY(cancel(fixture, gate));
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (starting) start(player, 42); else end(player, 42, MPV_END_FILE_REASON_STOP);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
  }
#endif
}
void TestHealthyRestoreCleanup::lateEntryDuringTerminalQuery_data()
{
  QTest::addColumn<bool>("starting"); QTest::newRow("end") << false; QTest::newRow("start") << true;
}

void TestHealthyRestoreCleanup::terminalOrdering()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(bool, endFirst);
  {
    HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
    loadReply(player, 42); start(player, 42); QVERIFY(cancel(fixture, gate));
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (endFirst) end(player, 42, MPV_END_FILE_REASON_STOP);
    reply(player, quint64{1} << 55);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    if (!endFirst) end(player, 42, MPV_END_FILE_REASON_STOP);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    int idle = 1; qint64 position = -1;
    property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
    property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped); QCOMPARE(stops, 1);
  }
#endif
}
void TestHealthyRestoreCleanup::terminalOrdering_data()
{
  QTest::addColumn<bool>("endFirst"); QTest::newRow("reply-first") << false; QTest::newRow("end-first") << true;
}

void TestHealthyRestoreCleanup::rejectedAndWrongReplies()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(bool, rejected);
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  loadReply(player, 42);
  if (rejected) player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return -1; };
  if (rejected) {
    const auto key = player.m_nativeRestore.key;
    QByteArray response; QVERIFY(fixture.cancel(11, 5, response)); QVERIFY(gate->matchesVerifiedCancel(11, 5));
    const FixtureRestart::CancelResult expected{fixture.grant().run, 5, fixture.grant().grant, fixture.grant().revision, 11, key.host, key.core, key.revision, key.attempt,
        FixtureRestart::CancelOutcome::Failed, FixtureRestart::CancelFailure::CleanupUnconfirmed};
    QCOMPARE(response, FixtureRestart::encodeCancelResult(expected));
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); QCOMPARE(stops, 1); return;
  }
  QVERIFY(cancel(fixture, gate));
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  reply(player, quint64{1} << 56);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  reply(player, quint64{1} << 55, -1); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}
void TestHealthyRestoreCleanup::rejectedAndWrongReplies_data()
{ QTest::addColumn<bool>("rejected"); QTest::newRow("rejected") << true; QTest::newRow("wrong-reply") << false; }

void TestHealthyRestoreCleanup::pendingSeekBlocksSettlement()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  int seeks = 0;
  player.m_restoreDispatch = [&](quint64, const char** args) {
    if (QByteArray(args[0]) == "seek") ++seeks;
    else if (QByteArray(args[0]) == "stop") ++stops;
    return 0;
  };
  loadReply(player, 42); start(player, 42);
  QVERIFY(fixture.observe(1, 42, "0")); QVERIFY(fixture.publishBaseline());
  QVERIFY(fixture.armTarget()); QVERIFY(fixture.seek()); QCOMPARE(seeks, 1);
  QVERIFY(cancel(fixture, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(stops, 0);
  reply(player, quint64{1} << 61);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending); QCOMPARE(stops, 1);
  end(player, 42, MPV_END_FILE_REASON_STOP); reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  int idle = 1; qint64 position = -1;
  property(player, quint64{1} << 54, "idle-active", MPV_FORMAT_FLAG, &idle);
  property(player, quint64{1} << 53, "playlist-pos", MPV_FORMAT_INT64, &position);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Stopped);
#endif
}

void TestHealthyRestoreCleanup::duplicateAndReentrant()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  loadReply(player, 42);
  bool nestedAccepted = false;
  player.m_restoreDispatch = [&](quint64, const char**) {
    ++stops; nestedAccepted = cancel(fixture, gate); return 0;
  };
  QVERIFY(cancel(fixture, gate)); QCOMPARE(stops, 1); QVERIFY(!nestedAccepted);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  QCOMPARE(stops, 1); QVERIFY(!nestedAccepted);
  QVERIFY(!cancel(fixture, gate));
  reply(player, quint64{1} << 55); reply(player, quint64{1} << 55);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); QCOMPARE(stops, 1);
#endif
}

void TestHealthyRestoreCleanup::supersededAndEventLoss()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(int, scenario);
  {
    HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
    if (scenario == 2) {
      QVERIFY(cancel(fixture, gate)); mpv_event value{}; value.event_id = MPV_EVENT_QUEUE_OVERFLOW;
      event(player, value); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed); return;
    }
    if (scenario == 1) ++player.m_mpvGeneration; else player.m_playbackIntent.play(false);
    const auto owner = player.m_playbackIntent.snapshot();
    const auto generation = player.m_mpvGeneration;
    QVERIFY(!cancel(fixture, gate)); QCOMPARE(stops, 0);
    QVERIFY(gate->matchesVerifiedCancel(11, 5));
    QVERIFY(!player.m_restoreCleanup.claimed);
    QCOMPARE(player.pollNativeRestoreCancellation(), Status::Unstarted);
    const auto after = player.m_playbackIntent.snapshot();
    QCOMPARE(after.epoch, owner.epoch); QCOMPARE(after.request, owner.request);
    QCOMPARE(after.revision, owner.revision); QCOMPARE(after.desired, owner.desired);
    QCOMPARE(player.m_mpvGeneration, generation); QCOMPARE(stops, 0);
  }
#endif
}
void TestHealthyRestoreCleanup::supersededAndEventLoss_data()
{
  QTest::addColumn<int>("scenario"); QTest::newRow("intent") << 0;
  QTest::newRow("core") << 1; QTest::newRow("event-loss") << 2;
}

void TestHealthyRestoreCleanup::missingIdentityAndRedirect()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(bool, redirected);
  HealthyRestoreTestFixture fixture; int stops = 0; auto gate = prepare(fixture, stops, true); QVERIFY(gate); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  loadReply(player, redirected ? 42 : 0); if (redirected) start(player, 42);
  QVERIFY(cancel(fixture, gate)); QCOMPARE(player.pollNativeRestoreCancellation(), Status::Pending);
  if (!redirected) reply(player, quint64{1} << 55);
  end(player, redirected ? 42 : 0, redirected ? MPV_END_FILE_REASON_REDIRECT : MPV_END_FILE_REASON_STOP);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}
void TestHealthyRestoreCleanup::missingIdentityAndRedirect_data()
{ QTest::addColumn<bool>("redirected"); QTest::newRow("missing") << false; QTest::newRow("redirect") << true; }

void TestHealthyRestoreCleanup::disabledConfiguration()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player; int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  auto gate = std::make_shared<Gate>(&player, 100);
  QVERIFY(!player.beginNativeRestoreCleanup(gate, HealthyRestart::VerifiedCleanupCancel{11, 5}, 100, HealthyRestart::FailureCause::None));
  QCOMPARE(stops, 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), Status::Failed);
#endif
}

QTEST_GUILESS_MAIN(TestHealthyRestoreCleanup)
#include "test_healthy_restore_cancel.moc"
