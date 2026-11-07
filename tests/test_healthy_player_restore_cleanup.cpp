#include "player/PlayerComponent.h"
#include "restore_test_fixture.h"
#include <RestartWire.h>
#include <QtTest/QtTest>
#include <QScopeGuard>

class TestPlayerRestoreCleanup : public QObject
{
  Q_OBJECT
private:
  bool arm(HealthyRestoreTestFixture& fixture, bool load);
  bool readyToSeek(HealthyRestoreTestFixture& fixture);
  bool beginCleanup(HealthyRestoreTestFixture& fixture);
  bool failedCleanup(HealthyRestoreTestFixture& fixture);
  static void clearCallbacks(PlayerComponent& player, HealthyRestoreTestFixture& fixture);
private slots:
  void initTestCase();
  void newerLoadAndStop();
  void newerLoadAndStop_data();
  void independentPlayerIsolation();
  void staleCoreAndHost();
  void staleCoreAndHost_data();
  void seekSubmissionFailureCleanup();
  void asyncFailureAndReentrancy();
  void newerIntentAfterFailure();
  void systemModeUnavailable();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  void bindingMustPrecedeArm();
  void bindingMustPrecedeArm_data();
  void queuedStopReplyAfterContainment();
  void pendingLoadSettlesBeforeStop();
  void pendingLoadExpiresBeforeStop();
  void containedPendingLoadContinuesOnlyForCurrentOwner();
  void containedPendingLoadContinuesOnlyForCurrentOwner_data();
  void containedPendingLoadWithoutWakeExpires();
#endif
};

void TestPlayerRestoreCleanup::initTestCase() { HealthyRestoreTestFixture::initializeProcessCache(); }
void TestPlayerRestoreCleanup::clearCallbacks(PlayerComponent& player, HealthyRestoreTestFixture& fixture)
{
  player.m_restoreDispatch = {}; player.m_healthyStopDispatch = {}; player.m_healthyStopEventWait = {};
  player.m_healthyStopClock = {}; player.m_observationDispatch = {}; player.m_stopDispatch = {};
  auto state = fixture.state(); state->clockHook = {}; state->moduleHook = {}; state->endpointHook = {}; state->submissionHook = {};
}
bool TestPlayerRestoreCleanup::arm(HealthyRestoreTestFixture& fixture, bool load)
{ return fixture.initialize(false) && fixture.acceptGrantAndPermit() && (!load || fixture.load()); }
bool TestPlayerRestoreCleanup::beginCleanup(HealthyRestoreTestFixture& fixture)
{ QByteArray response; return fixture.cancel(2, 5, response) && response.isEmpty(); }
bool TestPlayerRestoreCleanup::failedCleanup(HealthyRestoreTestFixture& fixture)
{
  const auto key = fixture.player()->m_nativeRestore.key;
  const FixtureRestart::CancelResult expected{fixture.grant().run, 5, fixture.grant().grant, fixture.grant().revision, 2,
      key.host, key.core, key.revision, key.attempt, FixtureRestart::CancelOutcome::Failed,
      FixtureRestart::CancelFailure::CleanupUnconfirmed};
  QByteArray response;
  return fixture.cancel(2, 5, response) && response == FixtureRestart::encodeCancelResult(expected);
}
bool TestPlayerRestoreCleanup::readyToSeek(HealthyRestoreTestFixture& fixture)
{
  return arm(fixture, true) && fixture.replyLoad(42) && fixture.observe(1, 42, "0") &&
      fixture.publishBaseline() && fixture.armTarget();
}

void TestPlayerRestoreCleanup::newerLoadAndStop()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(bool, stop);
  HealthyRestoreTestFixture fixture;
  QVERIFY(arm(fixture, !stop)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  if (stop) {
    player.requestNativeStop(); const auto newer = player.m_playbackIntent.snapshot();
    QVERIFY(!beginCleanup(fixture)); QCOMPARE(fixture.state()->stops, 0u);
    QCOMPARE(player.m_playbackIntent.snapshot().revision, newer.revision);
    QCOMPARE(player.m_playbackIntent.snapshot().desired, newer.desired); return;
  }
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.load(QStringLiteral("new"), {{"autoplay", true}}, {}));
  const auto newer = player.m_playbackIntent.snapshot();
  const auto receipt = player.m_nativeReceipts.snapshot();
  const int before = stops;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(fixture));
#else
  player.cancelNativeRestore();
#endif
  QCOMPARE(stops, before);
  QCOMPARE(fixture.state()->stops, 0u);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, newer.revision);
  QCOMPARE(player.m_playbackIntent.snapshot().desired, newer.desired);
  QCOMPARE(player.m_nativeReceipts.snapshot().key.attempt, receipt.key.attempt);
#endif
}
void TestPlayerRestoreCleanup::newerLoadAndStop_data()
{ QTest::addColumn<bool>("stop"); QTest::newRow("newer-load") << false; QTest::newRow("newer-stop") << true; }

void TestPlayerRestoreCleanup::independentPlayerIsolation()
{
  HealthyRestoreTestFixture fixture; QVERIFY(arm(fixture, true)); auto& old = *fixture.player();
  const auto callbacks = qScopeGuard([&] { clearCallbacks(old, fixture); });
  PlayerComponent independent; independent.m_playbackIntent.play(false);
  const auto before = independent.m_playbackIntent.snapshot();
  old.m_playbackIntent.play(false); QVERIFY(!beginCleanup(fixture));
  const auto after = independent.m_playbackIntent.snapshot();
  QCOMPARE(after.epoch, before.epoch); QCOMPARE(after.request, before.request);
  QCOMPARE(after.revision, before.revision); QCOMPARE(after.desired, before.desired);
  QCOMPARE(fixture.state()->stops, 0u);
}

void TestPlayerRestoreCleanup::staleCoreAndHost()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  QFETCH(int, scenario);
  HealthyRestoreTestFixture fixture; QVERIFY(arm(fixture, scenario != 2)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  if (scenario == 0) ++player.m_mpvGeneration;
  else if (scenario == 1) player.revokeNativeReceiptHost();
  else { QVERIFY(player.componentInitialize()); QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection)); }
  const auto before = player.m_playbackIntent.snapshot();
  QVERIFY(!beginCleanup(fixture)); QCOMPARE(stops, 0); QCOMPARE(fixture.state()->stops, 0u);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, before.revision);
  if (scenario == 2) QCOMPARE(player.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Exiting);
#endif
}
void TestPlayerRestoreCleanup::staleCoreAndHost_data()
{
  QTest::addColumn<int>("scenario"); QTest::newRow("core") << 0; QTest::newRow("host") << 1; QTest::newRow("exiting") << 2;
}

void TestPlayerRestoreCleanup::seekSubmissionFailureCleanup()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture;
  QVERIFY(readyToSeek(fixture)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  player.m_restoreDispatch = [](quint64, const char**) { return -17; };
  QVERIFY(!fixture.seek());
  QVERIFY(player.m_nativeRestore.failed && !player.m_nativeRestore.cancelled);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  QVERIFY(!fixture.seek());
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Error, -22}; };
#ifdef HEALTHY_COPIED_SOURCE_TEST
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return -22; };
  QVERIFY(failedCleanup(fixture)); QVERIFY(fixture.gate()->matchesVerifiedCancel(2, 5));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(player.m_restoreCleanup.stopError, -22);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  QVERIFY(!beginCleanup(fixture)); QCOMPARE(stops, 1);
#else
  const auto cleanup = player.cancelNativeRestore();
  QCOMPARE(cleanup.status, PlayerComponent::NativeStopResult::Status::Error);
  QCOMPARE(cleanup.error, -22);
  QVERIFY(player.m_nativeRestore.cleanupAttempted && player.m_nativeRestore.cleanupObserved);
  QCOMPARE(player.m_nativeRestore.cleanupResult.error, -22);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  player.cancelNativeRestore();
  QCOMPARE(stops, 1);
#endif
#endif
}

void TestPlayerRestoreCleanup::asyncFailureAndReentrancy()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture;
  QVERIFY(readyToSeek(fixture)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(fixture.seek());
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 61) | player.m_nativeRestore.key.attempt; event.error = -9;
  player.observeNativeReceipt(&event);
  QVERIFY(player.m_nativeRestore.failed && !player.m_nativeRestore.seekOk);
  QCOMPARE(player.m_nativeRestore.seekReplyError, -9);
  int stops = 0;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  bool reenteredPending = false;
  player.m_restoreDispatch = [&](quint64, const char**) {
    ++stops; reenteredPending = player.pollNativeRestoreCancellation() == PlayerComponent::RestoreCleanupStatus::Pending;
    return -22;
  };
  QVERIFY(failedCleanup(fixture)); QVERIFY(fixture.gate()->matchesVerifiedCancel(2, 5));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QVERIFY(reenteredPending); QCOMPARE(stops, 1); QCOMPARE(player.m_nativeRestore.seekReplyError, -9);
#else
  player.m_stopDispatch = [&] {
    ++stops;
    player.cancelNativeRestore();
    return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Unavailable, 0};
  };
  const auto cleanup = player.cancelNativeRestore();
  QCOMPARE(cleanup.status, PlayerComponent::NativeStopResult::Status::Unavailable);
  QCOMPARE(stops, 1);
  QVERIFY(player.m_nativeRestore.cleanupAttempted && player.m_nativeRestore.cleanupObserved);
  QCOMPARE(player.m_nativeRestore.seekReplyError, -9);
#endif
#endif
}

void TestPlayerRestoreCleanup::newerIntentAfterFailure()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  HealthyRestoreTestFixture fixture;
  QVERIFY(readyToSeek(fixture)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  player.m_restoreDispatch = [](quint64, const char**) { return -17; };
  QVERIFY(!fixture.seek());
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.load(QStringLiteral("new"), {{"autoplay", true}}, {}));
  const auto newer = player.m_playbackIntent.snapshot();
  const int before = stops;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(fixture));
#else
  player.cancelNativeRestore();
#endif
  QCOMPARE(stops, before);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, newer.revision);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  QVERIFY(!player.m_nativeRestore.cleanupAttempted);
#endif
}

void TestPlayerRestoreCleanup::systemModeUnavailable()
{
#ifndef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(player.bindNativeReceiptHost(7));
  const auto before = player.m_playbackIntent.snapshot();
  const PlayerComponent::RestoreGrant grant{3, 4, before.revision, 1000, true};
  QVERIFY(!player.armNativeRestore(grant, 7, 2));
  QCOMPARE(player.cancelNativeRestore().status, PlayerComponent::NativeStopResult::Status::Unavailable);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, before.revision);
  QCOMPARE(player.m_playbackIntent.snapshot().desired, before.desired);
#endif
}

#ifdef HEALTHY_COPIED_SOURCE_TEST
void TestPlayerRestoreCleanup::bindingMustPrecedeArm()
{
  QFETCH(int, scenario);
  HealthyRestoreTestFixture fixture; QVERIFY(fixture.prepare(false)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  if (scenario == 0) {
    QVERIFY(fixture.initializeRestore()); auto gate = fixture.gate(); QVERIFY(gate);
    QVERIFY(!player.healthyBindRestoreCleanupGate(gate, fixture.state()->clockNs.load() + 40000000000LL));
    return;
  }
  QVERIFY(player.bindNativeReceiptHost(7));
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, false};
  if (scenario == 1) {
    QVERIFY(player.armNativeRestore(grant, 7, 2)); QVERIFY(!fixture.initializeRestore()); return;
  }
  bool armed = false, bindStage = false; unsigned hookCall = 0;
  const auto state = fixture.state(); const auto expectedCall = state->clockCalls + 2;
  state->clockHookAt = expectedCall;
  state->clockHook = [&] {
    hookCall = state->clockCalls;
    bindStage = fixture.cleanupBindPending();
    if (bindStage) armed = player.armNativeRestore(grant, 7, 2);
  };
  QVERIFY(!fixture.initializeRestore());
  QCOMPARE(hookCall, expectedCall); QVERIFY(bindStage);
  QVERIFY(armed);
}
void TestPlayerRestoreCleanup::bindingMustPrecedeArm_data()
{
  QTest::addColumn<int>("scenario"); QTest::newRow("before") << 0;
  QTest::newRow("after") << 1; QTest::newRow("reentrant") << 2;
}

void TestPlayerRestoreCleanup::queuedStopReplyAfterContainment()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(readyToSeek(fixture)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  quint64 stopId = 0;
  int dispatches = 0;
  player.m_restoreDispatch = [&](quint64 id, const char**) { stopId = id; ++dispatches; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  mpv_event stop{}; stop.event_id = MPV_EVENT_COMMAND_REPLY;
  bool deliver = false;
  player.m_healthyStopEventWait = [&]() -> mpv_event* {
    if (deliver) { deliver = false; return &stop; }
    return &none;
  };
  QVERIFY(beginCleanup(fixture));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QVERIFY(stopId);
  QCOMPARE(dispatches, 1);
  stop.reply_userdata = stopId;
  player.m_healthyStopLease->contain();
  deliver = true;
  QVERIFY(QMetaObject::invokeMethod(&player, "handleMpvEvents", Qt::QueuedConnection));
  QCoreApplication::processEvents();
  QVERIFY(!deliver);
  QVERIFY(player.m_restoreCleanup.stopReply);
  QVERIFY(player.m_restoreCleanup.stopOk);
  QCOMPARE(player.m_restoreCleanup.replyError, 0);
  QVERIFY(player.m_healthyStopLease->contained());
  QCOMPARE(player.m_restoreCleanup.status, PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(dispatches, 1);
  QVERIFY(!player.m_restoreCleanup.idle.requested);
  QVERIFY(!player.m_restoreCleanup.position.requested);
  player.m_healthyStopEventWait = {};
}

void TestPlayerRestoreCleanup::pendingLoadSettlesBeforeStop()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(arm(fixture, true)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  int stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&] { return &none; };
  QVERIFY(beginCleanup(fixture));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 0);

  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = 42;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY;
  reply.reply_userdata = (quint64{1} << 63) | player.m_nativeRestore.key.attempt; reply.data = &command;
  player.observeNativeReceipt(&reply);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 1);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 1);
  QVERIFY(!player.m_nativeReceipts.snapshot().started);
  player.m_healthyStopEventWait = {};
}

void TestPlayerRestoreCleanup::pendingLoadExpiresBeforeStop()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(arm(fixture, true)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  qint64 now = fixture.state()->clockNs.load();
  player.m_healthyStopClock = [&] { return now; };
  int stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&] { return &none; };
  QVERIFY(beginCleanup(fixture));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 0);
  now += 50000000000LL;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(stops, 0);
  player.m_healthyStopEventWait = {};
}

void TestPlayerRestoreCleanup::containedPendingLoadContinuesOnlyForCurrentOwner()
{
  QFETCH(int, scenario);
  {
    HealthyRestoreTestFixture fixture;
    QVERIFY(arm(fixture, true)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
    qint64 now = fixture.state()->clockNs.load();
    player.m_healthyStopClock = [&] { return now; };
    int stops = 0; quint64 stopId = 0;
    player.m_restoreDispatch = [&](quint64 id, const char**) { ++stops; stopId = id; return 0; };
    mpv_event none{}; none.event_id = MPV_EVENT_NONE;
    mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = 42;
    char field[] = "playlist_entry_id"; char* fields[] = {field};
    mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
    mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
    mpv_event load{}; load.event_id = MPV_EVENT_COMMAND_REPLY;
    load.reply_userdata = (quint64{1} << 63) | player.m_nativeRestore.key.attempt; load.data = &command;
    mpv_event stop{}; stop.event_id = MPV_EVENT_COMMAND_REPLY;
    mpv_event* next = nullptr;
    player.m_healthyStopEventWait = [&]() -> mpv_event* {
      if (!next) return &none;
      auto* event = next; next = nullptr; return event;
    };
    QVERIFY(beginCleanup(fixture));
    QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
    QCOMPARE(stops, 0);
    player.m_healthyStopLease->contain();
    if (scenario == 1) now += 50000000000LL;
    if (scenario == 2) ++player.m_mpvGeneration;
    next = &load;
    QVERIFY(QMetaObject::invokeMethod(&player, "handleMpvEvents", Qt::QueuedConnection));
    QCoreApplication::processEvents();
    QVERIFY(!next);
    if (scenario != 0) {
      QCOMPARE(stops, 0);
      QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
    } else {
      QCOMPARE(stops, 1); QVERIFY(stopId);
      stop.reply_userdata = stopId; next = &stop;
      QVERIFY(QMetaObject::invokeMethod(&player, "handleMpvEvents", Qt::QueuedConnection));
      QCoreApplication::processEvents();
      QVERIFY(!next); QVERIFY(player.m_restoreCleanup.stopReply);
      QVERIFY(player.m_restoreCleanup.stopOk);
      QCOMPARE(player.m_restoreCleanup.replyError, 0);
      QCOMPARE(player.m_restoreCleanup.status, PlayerComponent::RestoreCleanupStatus::Pending);
      QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
      QCOMPARE(stops, 1);
    }
    QVERIFY(player.m_healthyStopLease->contained());
    QVERIFY(!player.m_restoreCleanup.idle.requested);
    QVERIFY(!player.m_restoreCleanup.position.requested);
    player.m_healthyStopEventWait = {};
  }
}
void TestPlayerRestoreCleanup::containedPendingLoadContinuesOnlyForCurrentOwner_data()
{
  QTest::addColumn<int>("scenario"); QTest::newRow("current") << 0;
  QTest::newRow("deadline") << 1; QTest::newRow("new-core") << 2;
}

void TestPlayerRestoreCleanup::containedPendingLoadWithoutWakeExpires()
{
  HealthyRestoreTestFixture fixture;
  QVERIFY(arm(fixture, true)); auto& player = *fixture.player(); const auto callbacks = qScopeGuard([&] { clearCallbacks(player, fixture); });
  qint64 now = fixture.state()->clockNs.load();
  player.m_healthyStopClock = [&] { return now; };
  int stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  int reads = 0;
  player.m_healthyStopEventWait = [&] { ++reads; return &none; };
  QVERIFY(beginCleanup(fixture));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 0);
  const auto claim = player.m_healthyStopLease;
  const auto key = player.m_restoreCleanup.key;
  QVERIFY(player.m_restoreCleanup.stopAdmitted);
  claim->contain();
  const int priorReads = reads;
  now += 50000000000LL; // No consumer wake occurs before the original deadline.
  QVERIFY(reads == priorReads && stops == 0);
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(reads, priorReads);
  QVERIFY(player.m_healthyStopLease == claim && claim->contained());
  const auto retained = player.m_restoreCleanup.key;
  QVERIFY(retained.host == key.host && retained.core == key.core &&
          retained.revision == key.revision && retained.attempt == key.attempt);
  QCOMPARE(stops, 0);
  QVERIFY(!player.m_restoreCleanup.idle.requested && !player.m_restoreCleanup.position.requested);
  player.m_healthyStopEventWait = {};
}
#endif

QTEST_GUILESS_MAIN(TestPlayerRestoreCleanup)
#include "test_healthy_player_restore_cleanup.moc"
