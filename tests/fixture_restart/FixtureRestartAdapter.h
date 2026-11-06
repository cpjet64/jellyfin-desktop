#pragma once
#include "RestartBroker.h"
#include <QString>
#include <functional>

class PlayerComponent;

// Fixture-only bridge. Its gate must be bound to the inherited owned channel.
class FixtureRestartAdapter {
public:
  FixtureRestartAdapter(PlayerComponent& player, FixtureRestart::ChildGate& gate,
                        quint64 host, quint64 attempt);
  FixtureRestartAdapter(PlayerComponent& player, FixtureRestart::ChildGate& gate,
                        quint64 host, quint64 attempt, quint64 launch,
                        QByteArray digest, std::function<qint64()> clock);
  bool arm();
  bool load(const QString& resolvedFixturePath);
  bool seek();
  bool cancel();
  bool loadA(const QString& path, const QByteArray& expectedHash, bool paused);
  bool captureA(const FixtureRestart::Grant& binding, FixtureRestart::Evidence& output);
  bool internalReplacementStop(const FixtureRestart::Evidence& frozen);
private:
  static bool checkedStop(void* context);
  bool current() const;
  PlayerComponent& player_;
  FixtureRestart::ChildGate& gate_;
  quint64 host_, attempt_, expectedRevision_, parentRevision_ = 0;
  quint64 launch_ = 0;
  QByteArray digest_;
  std::function<qint64()> clock_;
  bool armed_ = false;
  bool aLoad_ = false;
  QString aPath_;
  QByteArray aHash_;
};
