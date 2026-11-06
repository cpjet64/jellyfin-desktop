#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>
#ifdef HEALTHY_COPIED_SOURCE_TEST
#include <RestartBroker.h>
#include <memory>
#endif

class TestPlayerRestoreCleanup : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private:
  static bool arm(PlayerComponent& player, bool load);
  static bool readyToSeek(PlayerComponent& player);
#ifdef HEALTHY_COPIED_SOURCE_TEST
  static bool beginCleanup(PlayerComponent& player);
#endif
private slots:
  void newerLoadAndStop();
  void staleCoreAndHost();
  void seekSubmissionFailureCleanup();
  void asyncFailureAndReentrancy();
  void newerIntentAfterFailure();
  void systemModeUnavailable();
};

bool TestPlayerRestoreCleanup::arm(PlayerComponent& player, bool load)
{
#ifdef MPVQT_EXTERNAL_EVENTS
#ifdef HEALTHY_COPIED_SOURCE_TEST
  auto gate = std::make_shared<FixtureRestart::ChildGate>(&player, 100);
  player.m_healthyStopClock = [] { return qint64(10); };
  player.m_restoreDispatch = [](quint64, const char**) { return 0; };
  if (!player.healthyBindRestoreCleanupGate(gate)) return false;
#endif
  if (!player.bindNativeReceiptHost(7)) return false;
  player.m_mpvGeneration = 8;
  const PlayerComponent::RestoreGrant grant{3, 4, 0, 3000, false};
  if (!player.armNativeRestore(grant, 7, 2)) return false;
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
  auto gate = std::const_pointer_cast<FixtureRestart::ChildGate>(player.m_restoreCleanupGate);
  return gate && gate->cancel([](void*) { return true; }, nullptr) &&
      player.beginNativeRestoreCancellation(gate, 1, 100);
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

QTEST_GUILESS_MAIN(TestPlayerRestoreCleanup)
#include "test_player_restore_cleanup.moc"
