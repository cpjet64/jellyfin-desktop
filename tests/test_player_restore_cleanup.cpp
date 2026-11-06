#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <RestartChild.h>
#include <QCryptographicHash>
#include <map>
#include <memory>
#endif

class TestPlayerRestoreCleanup : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private:
  bool arm(PlayerComponent& player, bool load);
  bool readyToSeek(PlayerComponent& player);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  struct Harness {
    std::shared_ptr<FixtureRestart::ChildGate> gate;
    std::unique_ptr<FixtureRestart::RestartChild> child;
  };
  std::map<PlayerComponent*, std::unique_ptr<Harness>> harnesses_;
  bool beginCleanup(PlayerComponent& player);
#endif
private slots:
  void newerLoadAndStop();
  void staleCoreAndHost();
  void seekSubmissionFailureCleanup();
  void asyncFailureAndReentrancy();
  void newerIntentAfterFailure();
  void systemModeUnavailable();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  void bindingMustPrecedeArm();
  void queuedStopReplyAfterContainment();
  void pendingLoadSettlesBeforeStop();
  void pendingLoadExpiresBeforeStop();
#endif
};

bool TestPlayerRestoreCleanup::arm(PlayerComponent& player, bool load)
{
#ifdef MPVQT_EXTERNAL_EVENTS
#ifdef HEALTHY_COPIED_SOURCE_TEST
  auto harness = std::make_unique<Harness>();
  auto gate = harness->gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  player.m_healthyStopClock = [] { return qint64(10); };
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  if (!player.healthyBindRestoreCleanupGate(gate, 100)) return false;
#endif
  if (!player.bindNativeReceiptHost(7)) return false;
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, false};
#ifdef HEALTHY_COPIED_SOURCE_TEST
  using Child = FixtureRestart::RestartChild;
  const FixtureRestart::Grant binding{1, grant.id, grant.parentRevision, QByteArray(64, 'a'), grant.positionMs, grant.desiredPaused};
  const auto frame = FixtureRestart::encodeGrant(binding);
  const auto digest = QCryptographicHash::hash(frame, QCryptographicHash::Sha256);
  Child::Actions actions; actions.now = [] { return qint64(10); };
  actions.armActual = [&player, grant] {
    if (!player.armNativeRestore(grant, 7, 2)) return Child::ArmResult{};
    const auto key = player.m_nativeRestore.key;
    return Child::ArmResult{Child::ArmResult::Status::Armed, {key.host, key.core, key.revision, key.attempt}};
  };
  actions.beginCleanup = [&player, gate](quint64 seq, quint64 rev) {
    return player.beginNativeRestoreCleanup(gate, HealthyRestart::VerifiedCleanupCancel{seq, rev}, 100, HealthyRestart::FailureCause::None);
  };
  actions.pollCleanup = [] { return Child::CleanupResult{Child::CleanupResult::Status::Pending, {}, FixtureRestart::CancelFailure::None}; };
  harness->child = std::make_unique<Child>(*gate, gate.get(), &player, binding.run, 9, digest, std::move(actions));
  QByteArray response;
  if (!harness->child->grant(gate.get(), frame, 10, true, response)) return false;
  if (!player.healthyInstallRetirementClaim(std::make_shared<HealthyRetirement::RetirementClaim>(1), 1)) return false;
  harnesses_[&player] = std::move(harness);
#else
  if (!player.armNativeRestore(grant, 7, 2)) return false;
#endif
  if (!load) return true;
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  return player.dispatchNativeRestoreLoad(QStringLiteral("fixture"));
#else
  Q_UNUSED(player); Q_UNUSED(load); return false;
#endif
}

#ifdef HEALTHY_COPIED_SOURCE_TEST
bool TestPlayerRestoreCleanup::beginCleanup(PlayerComponent& player)
{
  const auto it = harnesses_.find(&player);
  if (it == harnesses_.end()) return false;
  QByteArray response;
  return it->second->child->control(&player,
      FixtureRestart::encodeControl({FixtureRestart::ControlKind::UserStop, 1, 5}), response) && response.isEmpty();
}
#endif

bool TestPlayerRestoreCleanup::readyToSeek(PlayerComponent& player)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!arm(player, true)) return false;
  player.m_nativeReceipts.reply(2, true, 42);
  player.m_nativeReceipts.start(42);
  player.m_nativeObservation.key = player.m_nativeReceipts.snapshot().key;
  player.m_nativeObservation.valid = true;
  player.m_nativeObservation.paused = true;
  return true;
#else
  Q_UNUSED(player); return false;
#endif
}

void TestPlayerRestoreCleanup::newerLoadAndStop()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(arm(player, true));
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.load(QStringLiteral("new"), {{"autoplay", true}}, {}));
  const auto newer = player.m_playbackIntent.snapshot();
  const auto receipt = player.m_nativeReceipts.snapshot();
  const int before = stops;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(player));
#else
  player.cancelNativeRestore();
#endif
  QCOMPARE(stops, before);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, newer.revision);
  QCOMPARE(player.m_playbackIntent.snapshot().desired, newer.desired);
  QCOMPARE(player.m_nativeReceipts.snapshot().key.attempt, receipt.key.attempt);
  PlayerComponent stopped;
  QVERIFY(arm(stopped, false));
  stopped.requestNativeStop();
  const auto stoppedIntent = stopped.m_playbackIntent.snapshot();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(stopped));
#else
  stopped.cancelNativeRestore();
#endif
  QCOMPARE(stopped.m_playbackIntent.snapshot().revision, stoppedIntent.revision);
  QCOMPARE(stopped.m_playbackIntent.snapshot().desired, stoppedIntent.desired);
#endif
}

void TestPlayerRestoreCleanup::staleCoreAndHost()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent core;
  QVERIFY(arm(core, true));
  int stops = 0;
  core.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  ++core.m_mpvGeneration;
  const auto before = core.m_playbackIntent.snapshot();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(core));
#else
  core.cancelNativeRestore();
#endif
  QCOMPARE(stops, 0);
  QCOMPARE(core.m_playbackIntent.snapshot().revision, before.revision);
  PlayerComponent host;
  QVERIFY(arm(host, true));
  host.m_stopDispatch = core.m_stopDispatch;
  host.revokeNativeReceiptHost();
  const auto hostBefore = host.m_playbackIntent.snapshot();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(host));
#else
  host.cancelNativeRestore();
#endif
  QCOMPARE(stops, 0);
  QCOMPARE(host.m_playbackIntent.snapshot().revision, hostBefore.revision);
  PlayerComponent exited;
  QVERIFY(arm(exited, false));
  QVERIFY(exited.componentInitialize());
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  const auto exitBefore = exited.m_playbackIntent.snapshot();
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(exited));
#else
  exited.cancelNativeRestore();
#endif
  QCOMPARE(exited.m_playbackIntent.snapshot().revision, exitBefore.revision);
  QCOMPARE(exited.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Exiting);
#endif
}

void TestPlayerRestoreCleanup::seekSubmissionFailureCleanup()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  PlayerComponent player;
  QVERIFY(readyToSeek(player));
  player.m_restoreDispatch = [](quint64, const char**) { return -17; };
  QVERIFY(!player.dispatchNativeRestoreSeek());
  QVERIFY(player.m_nativeRestore.failed && !player.m_nativeRestore.cancelled);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  QVERIFY(!player.dispatchNativeRestoreSeek());
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Error, -22}; };
#ifdef HEALTHY_COPIED_SOURCE_TEST
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return -22; };
  QVERIFY(beginCleanup(player));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(player.m_restoreCleanup.stopError, -22);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  QVERIFY(!beginCleanup(player)); QCOMPARE(stops, 1);
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
  PlayerComponent player;
  QVERIFY(readyToSeek(player));
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  QVERIFY(player.dispatchNativeRestoreSeek());
  mpv_event event{}; event.event_id = MPV_EVENT_COMMAND_REPLY;
  event.reply_userdata = (quint64{1} << 61) | 2; event.error = -9;
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
  QVERIFY(beginCleanup(player));
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
  PlayerComponent player;
  QVERIFY(readyToSeek(player));
  player.m_restoreDispatch = [](quint64, const char**) { return -17; };
  QVERIFY(!player.dispatchNativeRestoreSeek());
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  QVERIFY(player.load(QStringLiteral("new"), {{"autoplay", true}}, {}));
  const auto newer = player.m_playbackIntent.snapshot();
  const int before = stops;
#ifdef HEALTHY_COPIED_SOURCE_TEST
  QVERIFY(!beginCleanup(player));
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
  PlayerComponent before;
  before.m_healthyStopClock = [] { return qint64(10); };
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&before, 100);
  QVERIFY(before.healthyBindRestoreCleanupGate(gate, 100));
  QVERIFY(!before.healthyBindRestoreCleanupGate(gate, 100));

  PlayerComponent after;
  QVERIFY(after.bindNativeReceiptHost(7)); after.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, false};
  QVERIFY(after.armNativeRestore(grant, 7, 2));
  after.m_healthyStopClock = [] { return qint64(10); };
  auto late = std::make_shared<FixtureRestart::ChildGate>(&after, 100);
  QVERIFY(!after.healthyBindRestoreCleanupGate(late, 100));

  PlayerComponent reentrant;
  QVERIFY(reentrant.bindNativeReceiptHost(7)); reentrant.m_mpvGeneration = 8;
  bool armed = false;
  reentrant.m_healthyStopClock = [&] { armed = reentrant.armNativeRestore(grant, 7, 2); return qint64(10); };
  auto during = std::make_shared<FixtureRestart::ChildGate>(&reentrant, 100);
  QVERIFY(!reentrant.healthyBindRestoreCleanupGate(during, 100));
  QVERIFY(armed);
}

void TestPlayerRestoreCleanup::queuedStopReplyAfterContainment()
{
  PlayerComponent player;
  QVERIFY(readyToSeek(player));
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
  QVERIFY(beginCleanup(player));
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
  PlayerComponent player;
  QVERIFY(arm(player, true));
  int stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&] { return &none; };
  QVERIFY(beginCleanup(player));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 0);

  mpv_node value{}; value.format = MPV_FORMAT_INT64; value.u.int64 = 42;
  char field[] = "playlist_entry_id"; char* fields[] = {field};
  mpv_node_list list{}; list.num = 1; list.keys = fields; list.values = &value;
  mpv_event_command command{}; command.result.format = MPV_FORMAT_NODE_MAP; command.result.u.list = &list;
  mpv_event reply{}; reply.event_id = MPV_EVENT_COMMAND_REPLY;
  reply.reply_userdata = (quint64{1} << 63) | 2; reply.data = &command;
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
  PlayerComponent player;
  QVERIFY(arm(player, true));
  qint64 now = 10;
  player.m_healthyStopClock = [&] { return now; };
  int stops = 0;
  player.m_restoreDispatch = [&](quint64, const char**) { ++stops; return 0; };
  mpv_event none{}; none.event_id = MPV_EVENT_NONE;
  player.m_healthyStopEventWait = [&] { return &none; };
  QVERIFY(beginCleanup(player));
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Pending);
  QCOMPARE(stops, 0);
  now = 100;
  QCOMPARE(player.pollNativeRestoreCancellation(), PlayerComponent::RestoreCleanupStatus::Failed);
  QCOMPARE(stops, 0);
  player.m_healthyStopEventWait = {};
}
#endif

QTEST_GUILESS_MAIN(TestPlayerRestoreCleanup)
#include "test_player_restore_cleanup.moc"
