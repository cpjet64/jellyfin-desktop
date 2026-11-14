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
    PlayerComponentRecovery* actor = nullptr;
    RendererRuntime::WakeRelay relay;
    RecoveryActorNativeEvents events;
    std::int64_t now = 1000;
    std::vector<std::uint64_t> queries;
    std::vector<std::pair<std::uint64_t, QStringList>> commands;
    std::vector<std::pair<QVariant, QVariant>> tracks;
    std::vector<std::pair<QVariant, PlayerComponent::MediaType>> reselects;
    std::vector<bool> pauses;
    std::vector<quint64> hooks;
    int getResult = 0;
    int commandResult = 0;

    Fixture()
    {
      bindActor();
      if (!player.tryBindMpvController(nullptr)) qFatal("Synthetic actor binding failed");
      player.m_playbackIntent.play(false);
      events.start(5);
      wake();
    }

    void bindActor()
    {
      actor = new PlayerComponentRecovery(&player);
      player.m_recovery = actor;
      player.m_stopDispatch = [] { return PlayerComponent::NativeStopResult{PlayerComponent::NativeStopResult::Status::Accepted, 0}; };
      player.ordinaryTracksDispatch_ = [this](const QVariant& audio, const QVariant& subtitle) { tracks.emplace_back(audio, subtitle); };
      player.ordinaryReselectDispatch_ = [this](const QVariant& value, PlayerComponent::MediaType target) {
        reselects.emplace_back(value, target);
      };
      player.ordinaryPauseDispatch_ = [this](bool paused) { pauses.push_back(paused); return 0; };
      player.ordinaryHookDispatch_ = [this](quint64 id) { hooks.push_back(id); };
      player.ordinaryLegacyHookDispatch_ = [this](const QString& id) { hooks.push_back(id.toULongLong()); };
      actor->testEnabled_ = true;
      actor->testClock_ = [this] { return now; };
      actor->nextNativeEvent_ = [this] { return events.next(); };
      actor->ordinaryGetDispatch_ = [this](std::uint64_t id, const char*, mpv_format) { queries.push_back(id); return getResult; };
      actor->ordinaryCommandDispatch_ = [this](std::uint64_t id, const char** args) {
        QStringList values;
        while (*args) values.push_back(QString::fromUtf8(*args++));
        commands.emplace_back(id, values);
        return commandResult;
      };
      actor->bindTestConsumer(&relay);
    }

    void wake() { relay.notify(); QCoreApplication::processEvents(); }
    void beforePlaylist() { events.playlistReply(queries.back(), {{5, true, "https://example.invalid/old"}}); wake(); }
    void afterPlaylist(std::int64_t entry = 20) { events.playlistReply(queries.back(), {{entry, true, "https://example.invalid/new"}}); wake(); }
    void replace() { player.load(QStringLiteral("https://example.invalid/new"), {{"autoplay", true}}, {}, 1, 2); }
    void finishNew(std::int64_t entry = 20, bool mapped = false, bool startFirst = false)
    {
      if (startFirst) { events.start(entry); wake(); afterPlaylist(entry); }
      if (mapped) events.commandReplyEntry(commands.back().first, entry);
      else events.commandReply(commands.back().first);
      if (!startFirst) events.start(entry);
      wake();
      if (!startFirst) afterPlaylist(entry);
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
  void simulatedBindingRevocation();
  void postStopSnapshotExcludesOldEntry();
  void controlsAfterUnqualifiedLoad_data();
  void controlsAfterUnqualifiedLoad();
  void mismatchedMappedEntry();
};

void TestPlayerRecoveryActor::pendingPreferences_data()
{
  QTest::addColumn<bool>("submitted");
  QTest::addColumn<bool>("paused");
  QTest::addColumn<bool>("mapped");
  QTest::addColumn<bool>("startFirst");
  for (bool mapped : {false, true}) {
    const char* prefix = mapped ? "mapped" : "entryless-fallback";
    for (bool startFirst : {false, true}) {
      const char* order = startFirst ? "start-first" : "reply-first";
      QTest::newRow(qPrintable(QString("%1-%2-pause-before").arg(prefix, order))) << false << true << mapped << startFirst;
      QTest::newRow(qPrintable(QString("%1-%2-pause-after").arg(prefix, order))) << true << true << mapped << startFirst;
      QTest::newRow(qPrintable(QString("%1-%2-resume-before").arg(prefix, order))) << false << false << mapped << startFirst;
      QTest::newRow(qPrintable(QString("%1-%2-resume-after").arg(prefix, order))) << true << false << mapped << startFirst;
    }
  }
}

void TestPlayerRecoveryActor::pendingPreferences()
{
  QFETCH(bool, submitted);
  QFETCH(bool, paused);
  QFETCH(bool, mapped);
  QFETCH(bool, startFirst);
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
  f.finishNew(mapped ? INT64_C(4294967317) : 20, mapped, startFirst);
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, intent.revision);
  QVERIFY(f.player.m_playbackIntent.snapshot().reason != PlaybackIntent::Reason::UnattributedEvent);
  QCOMPARE(f.hooks.size(), std::size_t{1});
  QVERIFY(!f.tracks.empty());
  QCOMPARE(f.tracks.back().first.toInt(), 11);
  QCOMPARE(f.tracks.back().second.toInt(), 12);
  QVERIFY(!f.pauses.empty());
  QCOMPARE(bool(f.pauses.back()), paused);
  if (!mapped) QVERIFY(!f.actor->runtime_.activeKey().session);
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
  f.tracks.clear();
  const auto entry = change == 3 || change == 4 ? 20 : 5;
  const auto reason = change == 3 ? MPV_END_FILE_REASON_EOF :
                      change == 4 ? MPV_END_FILE_REASON_ERROR : MPV_END_FILE_REASON_STOP;
  f.events.end(entry, reason, change == 4 ? MPV_ERROR_LOADING_FAILED : 0);
  f.wake();
  QVERIFY(f.player.m_playbackIntent.snapshot().revision > previous.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::UnattributedEvent);
  QVERIFY(f.tracks.empty());
  if (change == 0) QCOMPARE(f.player.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Stopped);
}

void TestPlayerRecoveryActor::simulatedBindingRevocation()
{
  Fixture f;
  f.replace();
  f.beforePlaylist();
  QVERIFY(!f.commands.empty());
  const auto oldReply = f.commands.back().first;
  f.player.pause();
  f.player.setAudioStream(11);
  f.player.releaseMpvController(nullptr);
  f.bindActor();
  QVERIFY(f.player.tryBindMpvController(nullptr));
  f.player.load(QStringLiteral("https://example.invalid/rebound"), {{"autoplay", true}}, {}, 31, 32);
  const auto before = f.player.m_playbackIntent.snapshot();
  f.tracks.clear();
  f.events.commandReply(oldReply);
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QVERIFY(f.player.m_playbackIntent.snapshot().revision > before.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::UnattributedEvent);
  QVERIFY(f.tracks.empty());
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::postStopSnapshotExcludesOldEntry()
{
  Fixture f;
  f.replace();
  QVERIFY(!f.queries.empty());
  f.events.playlistReply(f.queries.back(), {});
  f.wake();
  QVERIFY(!f.commands.empty());
  f.player.pause();
  f.player.setAudioStream(11);
  f.player.setSubtitleStream(12);
  const auto revision = f.player.m_playbackIntent.snapshot().revision;
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, revision);
  f.finishNew();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(!f.tracks.empty());
  QCOMPARE(f.tracks.back().first.toInt(), 11);
  QCOMPARE(f.tracks.back().second.toInt(), 12);
  QVERIFY(!f.pauses.empty());
  QCOMPARE(bool(f.pauses.back()), true);
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::mismatchedMappedEntry()
{
  Fixture f;
  f.replace();
  f.beforePlaylist();
  QVERIFY(!f.commands.empty());
  f.player.pause();
  f.player.setAudioStream(11);
  f.player.setSubtitleStream(12);
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.events.commandReplyEntry(f.commands.back().first, INT64_C(4294967317));
  f.events.start(INT64_C(4294967318));
  f.wake();
  f.afterPlaylist(INT64_C(4294967318));
  f.tracks.clear();
  f.pauses.clear();
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
  f.events.preloadHook(std::uint64_t{17});
#else
  f.events.preloadHook(std::string("17"));
#endif
  f.events.fileLoaded();
  f.wake();
  QVERIFY(f.tracks.empty());
  QVERIFY(f.pauses.empty());
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::controlsAfterUnqualifiedLoad_data()
{
  QTest::addColumn<int>("failure");
  QTest::newRow("load-rejected") << 0;
  QTest::newRow("preflight-reply-error") << 1;
  QTest::newRow("preflight-timeout") << 2;
  QTest::newRow("preflight-dispatch-error") << 3;
}

void TestPlayerRecoveryActor::controlsAfterUnqualifiedLoad()
{
  QFETCH(int, failure);
  Fixture f;
  if (failure == 0) f.commandResult = MPV_ERROR_COMMAND;
  if (failure == 3) f.getResult = MPV_ERROR_PROPERTY_UNAVAILABLE;
  f.replace();
  if (failure == 0) f.beforePlaylist();
  if (failure == 1) { f.events.playlistError(f.queries.back()); f.wake(); }
  if (failure == 2) { f.now += 1501; f.actor->tick(); }
  QVERIFY(!f.commands.empty());
  f.pauses.clear();
  f.tracks.clear();
  f.reselects.clear();
  f.player.pause();
  f.player.play();
  f.player.setAudioStream(21);
  f.player.setSubtitleStream(22);
  QCOMPARE(f.pauses.size(), std::size_t{2});
  QCOMPARE(bool(f.pauses[0]), true);
  QCOMPARE(bool(f.pauses[1]), false);
  QCOMPARE(f.reselects.size(), std::size_t{2});
  QCOMPARE(f.reselects[0].first.toInt(), 21);
  QCOMPARE(f.reselects[0].second, PlayerComponent::MediaType::Audio);
  QCOMPARE(f.reselects[1].first.toInt(), 22);
  QCOMPARE(f.reselects[1].second, PlayerComponent::MediaType::Subtitle);
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

QTEST_GUILESS_MAIN(TestPlayerRecoveryActor)
#include "test_recovery_actor.moc"
