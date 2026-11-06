#include "RestartChildPlayer.h"
#include "FixtureRestartAdapter.h"
#include <utility>

namespace FixtureRestart {
RestartChildPlayer::RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate,
    const void* grantChannel, const void* controlChannel, Grant trustedBinding, QString fixturePath)
  : child_(gate, grantChannel, controlChannel, std::move(trustedBinding),
      {[&player] { return player.arm(); }, [&player] { return player.cancel(); },
       [&player, path = std::move(fixturePath)] { return player.load(path); },
       [&player] { return player.seek(); }})
{
}
RestartChildPlayer::RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate,
    const void* grantChannel, const void* controlChannel, quint64 expectedRun, quint64 launch,
    QByteArray expectedDigest, QString fixturePath, std::function<qint64()> clock)
  : child_(gate, grantChannel, controlChannel, expectedRun, launch, std::move(expectedDigest),
      {[&player] { return player.arm(); }, [&player] { return player.cancel(); },
       [&player, path = std::move(fixturePath)] { return player.load(path); },
       [&player] { return player.seek(); }, std::move(clock)})
{
}
RestartChildPlayer::RestartChildPlayer(FixtureRestartAdapter& player, ChildGate& gate,
    const void* grantChannel, const void* controlChannel, Grant trustedBinding, quint64 launch,
    QByteArray expectedDigest, QString fixturePath)
  : child_(gate, grantChannel, controlChannel, std::move(trustedBinding), launch, std::move(expectedDigest),
      {[&player] { return player.arm(); }, [&player] { return player.cancel(); },
       [&player, path = std::move(fixturePath)] { return player.load(path); },
       [&player] { return player.seek(); }})
{
}
bool RestartChildPlayer::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof)
{ return child_.grant(channel, frame, now, eof); }
RestartChildPlayer::RestartChildPlayer(ChildGate& gate, const void* grantChannel, const void* controlChannel,
    quint64 expectedRun, quint64 launch, QByteArray digest, RestartChild::Actions actions)
  : child_(gate, grantChannel, controlChannel, expectedRun, launch, std::move(digest), std::move(actions)) {}
bool RestartChildPlayer::grant(const void* channel, const QByteArray& frame, qint64 now, bool eof, QByteArray& response)
{ return child_.grant(channel, frame, now, eof, response); }
bool RestartChildPlayer::control(const void* channel, const QByteArray& request, QByteArray& response)
{ return child_.control(channel, request, response); }
bool RestartChildPlayer::pollCancellation(QByteArray& response) { return child_.pollCancellation(response); }
bool RestartChildPlayer::revoke() { return child_.revoke(); }
bool RestartChildPlayer::load() { return child_.load(); }
bool RestartChildPlayer::seek() { return child_.seek(); }
}
