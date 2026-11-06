#pragma once
#include "RestartWire.h"
#include <functional>

namespace FixtureRestart {
// One serialized fixture-owner event loop calls these methods. Channel tokens
// come from the retained process/channel allocation, not message contents.
class RestartChild {
public:
  struct Actions {
    std::function<bool()> arm, cancel, load, seek;
    std::function<qint64()> now;
  };
  RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
               Grant binding, Actions actions);
  RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
               Grant binding, quint64 launch, QByteArray expectedDigest, Actions actions);
  RestartChild(ChildGate& gate, const void* grantChannel, const void* controlChannel,
               quint64 expectedRun, quint64 launch, QByteArray expectedDigest, Actions actions);
  bool grant(const void* channel, const QByteArray& frame, qint64 now, bool eof);
  bool grant(const void* channel, const QByteArray& frame, qint64 now, bool eof,
             QByteArray& response);
  bool control(const void* channel, const QByteArray& request, QByteArray& response);
  bool revoke();
  bool load();
  bool seek();
private:
  bool allowed() const;
  ChildGate& gate_;
  const void* grantChannel_;
  const void* controlChannel_;
  Grant binding_;
  quint64 launch_ = 0;
  QByteArray expectedDigest_;
  Actions actions_;
  quint64 lastSequence_ = 0, currentRevision_ = 0;
  bool granted_ = false;
};
}
