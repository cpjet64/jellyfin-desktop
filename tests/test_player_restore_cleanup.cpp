#include "player/PlayerComponent.h"
#include "player_test_cache.h"
#include <QtTest/QtTest>

class TestPlayerRestoreCleanup : public QObject
{
  Q_OBJECT
  PlayerTestCache cache_;
private:
  static bool arm(PlayerComponent& player, bool load);
  static bool readyToSeek(PlayerComponent& player);
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
  player.cancelNativeRestore();
  QCOMPARE(stops, before);
  QCOMPARE(player.m_playbackIntent.snapshot().revision, newer.revision);
  QCOMPARE(player.m_playbackIntent.snapshot().desired, newer.desired);
  QCOMPARE(player.m_nativeReceipts.snapshot().key.attempt, receipt.key.attempt);
  PlayerComponent stopped;
  QVERIFY(arm(stopped, false));
  stopped.requestNativeStop();
  const auto stoppedIntent = stopped.m_playbackIntent.snapshot();
  stopped.cancelNativeRestore();
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
  core.cancelNativeRestore();
  QCOMPARE(stops, 0);
  QCOMPARE(core.m_playbackIntent.snapshot().revision, before.revision);
  PlayerComponent host;
  QVERIFY(arm(host, true));
  host.m_stopDispatch = core.m_stopDispatch;
  host.revokeNativeReceiptHost();
  const auto hostBefore = host.m_playbackIntent.snapshot();
  host.cancelNativeRestore();
  QCOMPARE(stops, 0);
  QCOMPARE(host.m_playbackIntent.snapshot().revision, hostBefore.revision);
  PlayerComponent exited;
  QVERIFY(arm(exited, false));
  QVERIFY(exited.componentInitialize());
  QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit", Qt::DirectConnection));
  const auto exitBefore = exited.m_playbackIntent.snapshot();
  exited.cancelNativeRestore();
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
  const auto cleanup = player.cancelNativeRestore();
  QCOMPARE(cleanup.status, PlayerComponent::NativeStopResult::Status::Error);
  QCOMPARE(cleanup.error, -22);
  QVERIFY(player.m_nativeRestore.cleanupAttempted && player.m_nativeRestore.cleanupObserved);
  QCOMPARE(player.m_nativeRestore.cleanupResult.error, -22);
  QCOMPARE(player.m_nativeRestore.seekError, -17);
  player.cancelNativeRestore();
  QCOMPARE(stops, 1);
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
  player.cancelNativeRestore();
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
