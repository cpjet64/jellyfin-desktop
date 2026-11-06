#pragma once
#ifdef _WIN32
#include "RestartBroker.h"
#include "RestartWire.h"
#include "WindowsRestartChannel.h"
#include "WindowsRestartIo.h"
#include "WindowsRestartProcess.h"
#include <string>

namespace FixtureRestart {
struct WindowsEndpoint { WindowsRestartChannel* channel = nullptr; WindowsRestartIo* io = nullptr; };

// All calls run on one owner event loop. Allocations and their endpoints stay
// retained on Pending or Unconfirmed; only cleanupOwned may release them.
class WindowsBrokerOperations final : public Operations {
public:
  WindowsBrokerOperations(WindowsRestartProcess& a, WindowsRestartProcess& b,
      WindowsEndpoint evidence, WindowsEndpoint aControl, WindowsEndpoint aAck,
      WindowsEndpoint bGrant, WindowsEndpoint bControl, WindowsEndpoint bAck,
      std::wstring executable, std::wstring arguments, quint64 revision);
  qint64 nowMs() const override;
  bool ownsAChannel(const void* channel) const override;
  bool validateEvidence(const Evidence& evidence) const override;
  Progress internalReplacementStop(qint64 deadline) override;
  Progress waitAAndChildren(qint64 deadline, bool& forced) override;
  Progress launchB(qint64 deadline) override;
  Progress transferToB(const QByteArray& frame, qint64 deadline) override;
  Progress cancelBAndAwaitAck(qint64 deadline) override;
  Progress cleanupOwned(qint64 deadline) override;
private:
  struct Exchange { int stage = 0; QByteArray request; };
  Progress exchange(Exchange& state, WindowsEndpoint write, WindowsEndpoint read,
                    const Control& control, qint64 deadline);
  Progress write(WindowsEndpoint endpoint, const QByteArray& bytes, int& stage, qint64 deadline);
  WindowsRestartProcess* a_;
  WindowsRestartProcess* b_;
  WindowsEndpoint evidence_, aControl_, aAck_, bGrant_, bControl_, bAck_;
  std::wstring executable_, arguments_;
  quint64 revision_;
  Exchange aStop_, bCancel_;
  int grantStage_ = 0;
  bool bLaunchStarted_ = false, aSettled_ = false, bSettled_ = false;
  bool valid_ = false;
};
}
#endif
