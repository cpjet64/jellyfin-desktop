#pragma once
#include "RestartChild.h"
#include <QString>

class FixtureRestartAdapter;
namespace FixtureRestart {
// Fixture-only bridge; the child owner constructs it on Player's control thread.
class RestartChildPlayer {
public:
  RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate, const void* grantChannel,
                     const void* controlChannel, Grant trustedBinding, QString fixturePath);
  RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate, const void* grantChannel,
                     const void* controlChannel, Grant trustedBinding, quint64 launch,
                     QByteArray expectedDigest, QString fixturePath);
  RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate, const void* grantChannel,
                     const void* controlChannel, quint64 expectedRun, quint64 launch,
                     QByteArray expectedDigest, QString fixturePath, std::function<qint64()> clock);
  RestartChildPlayer(ChildGate& gate, const void* grantChannel, const void* controlChannel,
                     quint64 expectedRun, quint64 launch, QByteArray expectedDigest,
                     RestartChild::Actions actions);
  bool grant(const void* channel, const QByteArray& frame, qint64 now, bool eof);
  bool grant(const void* channel, const QByteArray& frame, qint64 now, bool eof, QByteArray& response);
  bool control(const void* channel, const QByteArray& request, QByteArray& response);
  bool pollCancellation(QByteArray& response);
  bool revoke();
  bool load();
  bool seek();
private:
  RestartChild child_;
};
}
