#pragma once
#include "RestartWire.h"
#include <functional>

namespace FixtureRestart {
// A's evidence and internal cleanup are bound to separate owned channels.
class RestartSource {
public:
  using Capture = std::function<bool(Evidence&)>;
  using Stop = std::function<bool(const Evidence&)>;
  RestartSource(const void* evidenceChannel, const void* controlChannel, quint64 revision, Capture capture, Stop stop);
  bool freeze(const void* channel, QByteArray& frame);
  bool control(const void* channel, const QByteArray& request, QByteArray& response);
private:
  const void* evidenceChannel_;
  const void* controlChannel_;
  quint64 revision_;
  Capture capture_;
  Stop stop_;
  Evidence evidence_;
  bool captureAttempted_ = false, frozen_ = false, stopAttempted_ = false;
};
}
