#pragma once

#include "player/PlayerComponent.h"
#include "player/PlayerComponentRecovery.h"
#include "NativeEvents.h"
#include <QCoreApplication>
#include <QtTest/QtTest>
#include <cstdint>
#include <utility>
#include <vector>

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
  void retryNearClockLimit();
  void preparationClockRegression_data();
  void preparationClockRegression();
};
