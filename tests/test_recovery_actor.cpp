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
    std::vector<std::pair<QString, QVariant>> properties;
    std::vector<QStringList> playerCommands;
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
      player.ordinaryPropertyDispatch_ = [this](const QString& name, const QVariant& value) {
        properties.emplace_back(name, value); return 0;
      };
      player.ordinaryPlayerCommandDispatch_ = [this](const QStringList& args) { playerCommands.push_back(args); };
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
    bool property(const QString& name, const QVariant& value) const
    {
      for (const auto& item : properties) if (item.first == name && item.second == value) return true;
      return false;
    }
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
  void cancelledPendingLoad_data();
  void cancelledPendingLoad();
  void boundedPreparationRetries_data();
  void boundedPreparationRetries();
  void preparationDeadline();
  void malformedPreparationFails();
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
  if (submitted) { f.beforePlaylist(); QCOMPARE(f.commands.size(), std::size_t{1}); }
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
  if (!submitted) { f.beforePlaylist(); QCOMPARE(f.commands.size(), std::size_t{1}); }
  if (!submitted) QVERIFY(f.commands.back().second.back().contains(paused ? QStringLiteral("pause=yes") : QStringLiteral("pause=no")));
  f.finishNew(mapped ? INT64_C(4294967317) : 20, mapped, startFirst);
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, intent.revision);
  QVERIFY(f.player.m_playbackIntent.snapshot().reason != PlaybackIntent::Reason::UnattributedEvent);
  QCOMPARE(f.hooks.size(), std::size_t{1});
  QVERIFY(f.property(QStringLiteral("aid"), 11));
  QVERIFY(f.property(QStringLiteral("sid"), 12));
  QVERIFY(f.property(QStringLiteral("pause"), paused));
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
  f.properties.clear();
  const auto entry = change == 3 || change == 4 ? 20 : 5;
  const auto reason = change == 3 ? MPV_END_FILE_REASON_EOF :
                      change == 4 ? MPV_END_FILE_REASON_ERROR : MPV_END_FILE_REASON_STOP;
  f.events.end(entry, reason, change == 4 ? MPV_ERROR_LOADING_FAILED : 0);
  f.wake();
  QVERIFY(f.player.m_playbackIntent.snapshot().revision > previous.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::UnattributedEvent);
  QVERIFY(f.properties.empty());
  if (change == 0) QCOMPARE(f.player.m_playbackIntent.snapshot().desired, PlaybackIntent::Desired::Stopped);
}

void TestPlayerRecoveryActor::simulatedBindingRevocation()
{
  Fixture f;
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
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
  f.properties.clear();
  f.events.commandReply(oldReply);
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QVERIFY(f.player.m_playbackIntent.snapshot().revision > before.revision);
  QCOMPARE(f.player.m_playbackIntent.snapshot().reason, PlaybackIntent::Reason::UnattributedEvent);
  QVERIFY(f.properties.empty());
  QCOMPARE(failures.size(), 0);
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::cancelledPendingLoad_data()
{
  QTest::addColumn<int>("operation");
  QTest::newRow("seek") << 0;
  QTest::newRow("clear-queue") << 1;
  QTest::newRow("stop") << 2;
  QTest::newRow("new-item") << 3;
  QTest::newRow("start-baseline-changed") << 4;
  QTest::newRow("event-overflow") << 5;
}

void TestPlayerRecoveryActor::cancelledPendingLoad()
{
  QFETCH(int, operation);
  Fixture f;
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
  f.replace();
  QVERIFY(!f.queries.empty());
  const auto oldQuery = f.queries.back();
  if (operation == 0) f.player.seekTo(12000);
  else if (operation == 1) f.player.clearQueue();
  else if (operation == 2) f.player.stop();
  else if (operation == 3) f.player.load(QStringLiteral("https://example.invalid/successor"), {{"autoplay", true}}, {}, 31, 32);
  else { if (operation == 4) f.events.start(6); else f.events.overflow(); f.wake(); }
  const auto intent = f.player.m_playbackIntent.snapshot();
  QVERIFY(f.commands.empty());
  f.events.playlistReply(oldQuery, {{5, true, "https://example.invalid/old"}});
  f.wake();
  QVERIFY(f.commands.empty());
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, intent.revision);
  QCOMPARE(failures.size(), 0);
  QVERIFY(!f.actor->runtime_.activeKey().session);
}

void TestPlayerRecoveryActor::postStopSnapshotExcludesOldEntry()
{
  Fixture f;
  f.replace();
  QVERIFY(!f.queries.empty());
  f.events.playlistReply(f.queries.back(), {});
  f.wake();
  QCOMPARE(f.commands.size(), std::size_t{1});
  QCOMPARE(f.queries.size(), std::size_t{1});
  f.player.pause();
  f.player.setAudioStream(11);
  f.player.setSubtitleStream(12);
  const auto revision = f.player.m_playbackIntent.snapshot().revision;
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, revision);
  f.finishNew();
  QCOMPARE(f.player.m_playbackIntent.snapshot().revision, revision);
  QVERIFY(f.property(QStringLiteral("aid"), 11));
  QVERIFY(f.property(QStringLiteral("sid"), 12));
  QVERIFY(f.property(QStringLiteral("pause"), true));
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
  f.properties.clear();
  f.playerCommands.clear();
#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
  f.events.preloadHook(std::uint64_t{17});
#else
  f.events.preloadHook(std::string("17"));
#endif
  f.events.fileLoaded();
  f.wake();
  QVERIFY(f.properties.empty());
  QVERIFY(f.playerCommands.empty());
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
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
  if (failure == 0) f.commandResult = MPV_ERROR_COMMAND;
  if (failure == 3) f.getResult = MPV_ERROR_PROPERTY_UNAVAILABLE;
  f.replace();
  if (failure == 0) f.beforePlaylist();
  if (failure == 1) {
    for (int i = 0; i != 3; ++i) {
      f.events.playlistError(f.queries.back()); f.wake();
      if (i != 2) { f.now += 500; f.actor->tick(); }
    }
  }
  if (failure == 2) { f.now += 1501; f.actor->tick(); }
  if (failure == 3) { f.now += 500; f.actor->tick(); f.now += 500; f.actor->tick(); }
  if (failure == 0) { f.now += 2000; f.actor->tick(); QCOMPARE(f.queries.size(), std::size_t{1}); }
  if (failure == 1 || failure == 3) {
    QCOMPARE(f.queries.size(), std::size_t{3});
    QVERIFY(f.queries[0] != f.queries[1] && f.queries[1] != f.queries[2] && f.queries[0] != f.queries[2]);
  }
  QCOMPARE(f.commands.size(), failure == 0 ? std::size_t{1} : std::size_t{0});
  QCOMPARE(failures.size(), failure == 0 ? 0 : 1);
  if (failure == 0) QCOMPARE(f.player.m_playbackIntent.snapshot().dispatch, PlaybackIntent::Dispatch::NativeRejected);
  if (failure != 0) {
    const auto result = f.player.m_playbackIntent.snapshot();
    QCOMPARE(result.dispatch, failure == 2 ? PlaybackIntent::Dispatch::PreparationTimeout :
                                            PlaybackIntent::Dispatch::PreparationUnavailable);
    QCOMPARE(failures[0][0].toULongLong(), result.request);
    QCOMPARE(failures[0][1].toString(), failure == 2 ? QStringLiteral("preparation-timeout") :
                                                      QStringLiteral("preparation-unavailable"));
  }
  f.playerCommands.clear();
  f.properties.clear();
  f.player.pause();
  f.player.play();
  f.player.pause();
  f.player.setAudioStream(21);
  f.player.setSubtitleStream(22);
  QCOMPARE(f.playerCommands.size(), std::size_t{3});
  QCOMPARE(f.playerCommands[0], QStringList({"set", "pause", "yes"}));
  QCOMPARE(f.playerCommands[1], QStringList({"set", "pause", "no"}));
  QCOMPARE(f.playerCommands[2], QStringList({"set", "pause", "yes"}));
  QVERIFY(f.property(QStringLiteral("aid"), 21));
  QVERIFY(f.property(QStringLiteral("sid"), 22));
  QVERIFY(!f.actor->runtime_.activeKey().session);
  const auto count = f.commands.size();
  f.events.end(5, MPV_END_FILE_REASON_STOP);
  f.wake();
  QCOMPARE(f.commands.size(), count);
  QVERIFY(!f.actor->replacementTransition_);
  if (failure == 1 || failure == 3) {
    f.events.playlistReply(f.queries.back(), {}); f.wake();
    QCOMPARE(f.commands.size(), count);
    QCOMPARE(failures.size(), 1);
  }
}

void TestPlayerRecoveryActor::boundedPreparationRetries_data()
{
  QTest::addColumn<bool>("dispatchFailure");
  QTest::addColumn<bool>("mapped");
  QTest::newRow("dispatch-error-map") << true << true;
  QTest::newRow("dispatch-error-none") << true << false;
  QTest::newRow("reply-error-map") << false << true;
  QTest::newRow("reply-error-none") << false << false;
}

void TestPlayerRecoveryActor::boundedPreparationRetries()
{
  QFETCH(bool, dispatchFailure);
  QFETCH(bool, mapped);
  Fixture f;
  if (dispatchFailure) f.getResult = MPV_ERROR_PROPERTY_UNAVAILABLE;
  f.replace();
  QCOMPARE(f.queries.size(), std::size_t{1});
  const auto obsolete = f.queries.front();
  const auto deadline = f.actor->ordinaryLoad_.deadlineMs;
  if (!dispatchFailure) { f.events.playlistError(obsolete); f.wake(); }
  f.getResult = 0;
  f.player.pause(); f.player.setAudioStream(11); f.player.setSubtitleStream(12);
  QCOMPARE(f.actor->ordinaryLoad_.deadlineMs, deadline);
  f.now += 499; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{1});
  QVERIFY(f.commands.empty());
  ++f.now; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{2});
  QVERIFY(f.queries.back() != obsolete);
  f.events.playlistReply(obsolete, {}); f.wake();
  QVERIFY(f.commands.empty());
  f.events.playlistReply(f.queries.back(), {}); f.wake();
  QCOMPARE(f.commands.size(), std::size_t{1});
  QVERIFY(f.commands.back().second.back().contains(QStringLiteral("pause=yes")));
  f.events.end(5, MPV_END_FILE_REASON_STOP); f.wake();
  f.finishNew(mapped ? INT64_C(4294967317) : 20, mapped);
  QVERIFY(f.property(QStringLiteral("aid"), 11));
  QVERIFY(f.property(QStringLiteral("sid"), 12));
  QVERIFY(f.property(QStringLiteral("pause"), true));
}

void TestPlayerRecoveryActor::preparationDeadline()
{
  for (int atDeadline = 0; atDeadline != 2; ++atDeadline) {
    Fixture f;
    QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
    f.replace();
    QCOMPARE(f.queries.size(), std::size_t{1});
    const auto query = f.queries.front();
    f.now += 1499; f.actor->tick();
    QCOMPARE(f.queries.size(), std::size_t{1});
    if (atDeadline) { ++f.now; f.events.playlistReply(query, {}); f.wake(); }
    else { ++f.now; f.actor->tick(); f.events.playlistReply(query, {}); f.wake(); }
    QCOMPARE(f.queries.size(), std::size_t{1});
    QVERIFY(f.commands.empty());
    QCOMPARE(failures.size(), 1);
    QCOMPARE(failures[0][1].toString(), QStringLiteral("preparation-timeout"));
    QVERIFY(!f.actor->runtime_.activeKey().session);
  }
}

void TestPlayerRecoveryActor::malformedPreparationFails()
{
  Fixture f;
  QSignalSpy failures(&f.player, &PlayerComponent::loadPreparationFailed);
  f.replace();
  f.events.malformedPlaylist(f.queries.back()); f.wake();
  QCOMPARE(f.queries.size(), std::size_t{1});
  QVERIFY(f.commands.empty());
  QCOMPARE(failures.size(), 1);
  QCOMPARE(failures[0][1].toString(), QStringLiteral("preparation-unavailable"));
  f.now += 1000; f.actor->tick();
  QCOMPARE(f.queries.size(), std::size_t{1});
}

QTEST_GUILESS_MAIN(TestPlayerRecoveryActor)
#include "test_recovery_actor.moc"
