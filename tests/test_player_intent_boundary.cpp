#include "player/PlayerComponent.h"
#include "player/PlaybackCheckpoint.h"
#include "system/WebClientFaultGuard.h"
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>

class TestPlaybackIntent : public QObject
{
  Q_OBJECT
private slots:
  void replacementAndStopResults();
  void unexpectedThreadCannotMutateOrDispatch();
  void legacyMutationsStayUnqualified();
  void priorRendererLossIsUnqualified();
};

void TestPlaybackIntent::replacementAndStopResults()
{
  PlayerComponent player;
  using Status = PlayerComponent::NativeStopResult::Status;
  int stops = 0;
  player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{Status::Accepted, 0}; };
  QVERIFY(player.load("fixture", {{"autoplay", true}, {"startMilliseconds", 12000}}, {}));
  QCOMPARE(stops, 1);
  auto playing = player.m_playbackIntent.snapshot();
  QCOMPARE(playing.desired, PlaybackIntent::Desired::Playing);
  QVERIFY(playing.request > 0);
  QVERIFY(playing.hasSeek && playing.seekMs == 12000 && !playing.timelineQualified);
  QByteArray encoded;
  QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), encoded));
  PlaybackCheckpoint checkpoint;
  QVERIFY(PlaybackCheckpointCodec::decode(encoded, checkpoint));
  QCOMPARE(checkpoint.intent.request, playing.request);
  QVERIFY(!checkpoint.positionKnown && !checkpoint.tracksKnown && !checkpoint.queueIdentityKnown);
  QCOMPARE(playing.dispatch, PlaybackIntent::Dispatch::Unavailable);
  QVERIFY(player.load("second", {{"autoplay", false}}, {}));
  QCOMPARE(stops, 2);
  auto replacement = player.m_playbackIntent.snapshot();
  QVERIFY(replacement.request > playing.request);
  QCOMPARE(replacement.desired, PlaybackIntent::Desired::Paused);
  for (const auto status : {Status::Unavailable, Status::Error, Status::Accepted}) {
    player.m_stopDispatch = [&] { ++stops; return PlayerComponent::NativeStopResult{status, -7}; };
    player.stop();
    const auto snapshot = player.m_playbackIntent.snapshot();
    QCOMPARE(snapshot.desired, PlaybackIntent::Desired::Stopped);
    const auto expected = status == Status::Unavailable ? PlaybackIntent::Dispatch::Unavailable :
      status == Status::Error ? PlaybackIntent::Dispatch::NativeRejected : PlaybackIntent::Dispatch::NativeAccepted;
    QCOMPARE(snapshot.dispatch, expected);
    QVERIFY(PlaybackCheckpointCodec::encode(player.makePlaybackCheckpoint(), encoded));
    QVERIFY(PlaybackCheckpointCodec::decode(encoded, checkpoint));
    QCOMPARE(checkpoint.intent.desired, PlaybackIntent::Desired::Stopped);
    QVERIFY(!player.m_playbackIntent.automaticResumeEligible());
  }
  QCOMPARE(stops, 5);
}

void TestPlaybackIntent::legacyMutationsStayUnqualified()
{
  PlayerComponent player;
  QVERIFY(player.metaObject()->indexOfMethod("applyIntentEnvelope(QVariantMap)") < 0);
  QVERIFY(player.load("fixture", {{"autoplay", true}}, {}));
  auto before = player.m_playbackIntent.snapshot();
  player.pause();
  auto paused = player.m_playbackIntent.snapshot();
  QVERIFY(paused.revision > before.revision && !paused.timelineQualified);
  player.seekTo(1234);
  auto seek = player.m_playbackIntent.snapshot();
  QVERIFY(seek.revision > paused.revision && seek.hasSeek && seek.seekMs == 1234 && !seek.timelineQualified);
  QVERIFY(!player.m_playbackIntent.dispatch(paused.revision, PlaybackIntent::Dispatch::NativeAccepted));
  player.setAudioStream(1);
  player.setSubtitleStream(2);
  player.clearQueue();
  player.setWebPlaylist({}, "");
  const auto changed = player.m_playbackIntent.snapshot();
  QVERIFY(changed.revision > seek.revision && !changed.timelineQualified);
  QVERIFY(!player.m_playbackIntent.automaticResumeEligible());
}

void TestPlaybackIntent::priorRendererLossIsUnqualified()
{
  WebClientFaultGuard::instance().observeRendererLoss(0, 0);
  PlayerComponent player;
  QVERIFY(player.componentInitialize());
  QCOMPARE(player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::RendererLost);
  QVERIFY(!player.m_playbackIntent.automaticResumeEligible());
}

void TestPlaybackIntent::unexpectedThreadCannotMutateOrDispatch()
{
  PlayerComponent player;
  std::atomic<int> calls{0};
  player.m_stopDispatch = [&] { ++calls; return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
  const auto before = player.m_playbackIntent.snapshot();
  auto* worker = QThread::create([&] { player.stop(); });
  worker->start();
  const bool settled = worker->wait(3000);
  if (!settled) qFatal("intent test worker did not settle");
  delete worker;
  QCOMPARE(calls.load(), 0);
  const auto after = player.m_playbackIntent.snapshot();
  QCOMPARE(after.request, before.request);
  QCOMPARE(after.revision, before.revision);
  QCOMPARE(after.desired, before.desired);
}

QTEST_GUILESS_MAIN(TestPlaybackIntent)
#include "test_player_intent_boundary.moc"
