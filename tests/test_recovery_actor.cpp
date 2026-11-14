#include "player/PlayerComponent.h"
#include "player/PlayerComponentRecovery.h"
#include "recovery_actor/NativeEvents.h"
#include <QCoreApplication>
#include <QtTest/QtTest>

class TestPlayerRecoveryActor : public QObject {
  Q_OBJECT
private:
  struct Fixture {
    PlayerComponent player;
    PlayerComponentRecovery* actor = new PlayerComponentRecovery(&player);
    RendererRuntime::WakeRelay relay;
    RecoveryActorNativeEvents events;
    std::int64_t now = 1000;
    std::vector<std::uint64_t> queries;
    std::vector<std::pair<std::uint64_t, QStringList>> commands;
    std::vector<std::pair<QVariant, QVariant>> tracks;
    std::vector<bool> pauses;
    std::vector<quint64> hooks;

    Fixture()
    {
      player.m_recovery = actor;
      player.m_mpvGeneration = 1;
      player.m_stopDispatch = [] { return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
      player.ordinaryTracksDispatch_ = [this](const QVariant& audio, const QVariant& subtitle) { tracks.emplace_back(audio, subtitle); };
      player.ordinaryPauseDispatch_ = [this](bool paused) { pauses.push_back(paused); return 0; };
      player.ordinaryHookDispatch_ = [this](quint64 id) { hooks.push_back(id); };
      player.ordinaryLegacyHookDispatch_ = [this](const QString& id) { hooks.push_back(id.toULongLong()); };
      actor->testEnabled_ = true;
      actor->testClock_ = [this] { return now; };
      actor->nextNativeEvent_ = [this] { return events.next(); };
      actor->ordinaryGetDispatch_ = [this](std::uint64_t id, const char*, mpv_format) { queries.push_back(id); return 0; };
      actor->ordinaryCommandDispatch_ = [this](std::uint64_t id, const char** args) {
        QStringList values;
        while (*args) values.push_back(QString::fromUtf8(*args++));
        commands.emplace_back(id, values);
        return 0;
      };
      actor->bindTestConsumer(&relay);
      player.m_playbackIntent.play(false);
      events.start(5);
      wake();
    }

    void wake() { relay.notify(); QCoreApplication::processEvents(); }
    void beforePlaylist() { events.playlistReply(queries.back(), {{5, true, "https://example.invalid/old"}}); wake(); }
    void afterPlaylist() { events.playlistReply(queries.back(), {{20, true, "https://example.invalid/new"}}); wake(); }
    void replace() { player.load(QStringLiteral("https://example.invalid/new"), {{"autoplay", true}}, {}, 1, 2); }
    void finishNew()
    {
      events.commandReply(commands.back().first);
      events.start(20);
      wake();
      afterPlaylist();
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
      events.preloadHook(std::uint64_t{17});
#else
      events.preloadHook(std::string("17"));
#endif
      events.fileLoaded();
      wake();
    }
  };

private slots:
  void pendingPreferences_data();
  void pendingPreferences();
  void staleClassification_data();
  void staleClassification();
};

void TestPlayerRecoveryActor::pendingPreferences_data()
{
  QTest::addColumn<bool>("submitted");
  QTest::addColumn<bool>("paused");
  QTest::newRow("pause-before-submission") << false << true;
  QTest::newRow("pause-after-submission") << true << true;
  QTest::newRow("resume-before-submission") << false << false;
  QTest::newRow("resume-after-submission") << true << false;
}

void TestPlayerRecoveryActor::pendingPreferences()
{
  QFETCH(bool, submitted);
  QFETCH(bool, paused);
  Fixture f;
  f.replace();
  QVERIFY(!f.queries.empty());
  if (submitted) { f.beforePlaylist(); QVERIFY(!f.commands.empty()); }
  f.player.pause();
  if (!paused) f.player.play();
  f.player.setAudioStream(11);
  f.player.setSubtitleStream(12);
  const auto intent = f.player.m_playbackIntent.snapshot();
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, intent.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().desired,
           paused ? PlaybackIntent::Desired::Paused : PlaybackIntent::Desired::Playing);
  if (!submitted) { f.beforePlaylist(); QVERIFY(!f.commands.empty()); }
  if (!submitted) QVERIFY(f.commands.back().second.back().contains(paused ? QStringLiteral("pause=yes") : QStringLiteral("pause=no")));
  f.finishNew();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, intent.revision);
  QVERIFY(f.player.m_playbackIntent.snapshot().reason != PlaybackIntent::Reason::UnattributedEvent);
  QCOMPARE(f.hooks.size(), std::size_t{1});
  QVERIFY(!f.tracks.empty());
  QCOMPARE(f.tracks.back().first.toInt(), 11);
  QCOMPARE(f.tracks.back().second.toInt(), 12);
  QVERIFY(!f.pauses.empty());
  QCOMPARE(bool(f.pauses.back()), paused);
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::staleClassification_data()
{
  QTest::addColumn<int>("change");
  QTest::newRow("explicit-stop") << 0;
  QTest::newRow("new-item") << 1;
  QTest::newRow("expired") << 2;
  QTest::newRow("current-eof") << 3;
  QTest::newRow("current-error") << 4;
  QTest::newRow("rejected-replacement-stop") << 5;
}

void TestPlayerRecoveryActor::staleClassification()
{
  QFETCH(int, change);
  Fixture f;
  if (change == 5) f.player.m_stopDispatch = [] {
    return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Error, -1};
  };
  f.replace();
  if (change == 0) f.player.stop();
  if (change == 1) f.player.load(QStringLiteral("https://example.invalid/other"), {{"autoplay", true}}, {}, 3, 4);
  if (change == 2) f.now += 15001;
  if (change == 3 || change == 4) { f.beforePlaylist(); f.events.start(20); f.wake(); }
  const auto previous = f.player.m_playbackIntent.snapshot();
  const auto entry = change == 3 || change == 4 ? 20 : 5;
  const auto reason = change == 3 ? MPV_END_FILE_REASON_EOF :
                      change == 4 ? MPV_END_FILE_REASON_ERROR : MPV_END_FILE_REASON_STOP;
  f.events.end(entry, reason, change == 4 ? MPV_ERROR_LOADING_FAILED : 0);
  f.wake();
  QVERIFY(f.player.m_playbackIntent.snapshot().revision > previous.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::UnattributedEvent);
  if (change == 0) QCOMPARE(f.player.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Stopped);
}

QTEST_GUILESS_MAIN(TestPlayerRecoveryActor)
#include "test_recovery_actor.moc"
