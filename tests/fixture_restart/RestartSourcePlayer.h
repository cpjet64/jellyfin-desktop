#pragma once
#include "RestartSource.h"

class FixtureRestartAdapter;
namespace FixtureRestart {
class RestartSourcePlayer {
public:
  RestartSourcePlayer(FixtureRestartAdapter& player, const void* evidenceChannel,
                      const void* controlChannel, Grant binding);
  bool freeze(const void* channel, QByteArray& frame) { return source_.freeze(channel, frame); }
  bool control(const void* channel, const QByteArray& request, QByteArray& response)
  { return source_.control(channel, request, response); }
private:
  RestartSource source_;
};
}
