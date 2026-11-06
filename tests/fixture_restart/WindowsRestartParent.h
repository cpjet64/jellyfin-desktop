#pragma once
#ifdef _WIN32
#include "WindowsBrokerOperations.h"
#include "WindowsFrameReader.h"
#include <memory>

namespace FixtureRestart {
// Owner-loop orchestration only. Transferred means the one grant was written;
// it does not mean B loaded, rendered, resumed, or settled.
class WindowsRestartParent {
public:
  enum class State { Idle, Reading, Replacing, Transferred, Cleaning, Failed, Unconfirmed };
  enum class Failure { None, Channel, Frame, Evidence, Replacement, Canceled };
  WindowsRestartParent(WindowsBrokerOperations& operations, WindowsEndpoint evidence, Grant binding);
  bool begin();
  void step();
  Broker::Cancellation cancel();
  State state() const { return state_; }
  Failure failure() const { return failure_; }
  const Evidence& frozenEvidence() const { return broker_.frozenEvidence(); }
private:
  void fail(Failure failure);
  void finishRead(IoProgress progress);
  WindowsBrokerOperations& operations_;
  WindowsEndpoint evidence_;
  Grant binding_;
  std::unique_ptr<WindowsFrameReader> reader_;
  Broker broker_;
  State state_ = State::Idle;
  Failure failure_ = Failure::None;
};
}
#endif
