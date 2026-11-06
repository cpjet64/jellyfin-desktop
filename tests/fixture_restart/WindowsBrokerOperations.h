#pragma once
#ifdef _WIN32
#include "RestartBroker.h"
#include "RestartWire.h"
#include "WindowsRestartChannel.h"
#include "WindowsRestartIo.h"
#include "WindowsRestartProcess.h"
#include <string>
#include <functional>
#include <memory>

namespace FixtureRestart {
struct WindowsEndpoint { WindowsRestartChannel* channel = nullptr; WindowsRestartIo* io = nullptr; };

// All calls run on one owner event loop. Allocations and their endpoints stay
// retained on Pending or Unconfirmed; only cleanupOwned may release them.
class WindowsBrokerOperations final : public Operations {
public:
  WindowsBrokerOperations(WindowsRestartProcess& a, WindowsRestartProcess& b,
      WindowsEndpoint evidence, WindowsEndpoint aControl, WindowsEndpoint aAck,
      WindowsEndpoint bGrant, WindowsEndpoint bControl, WindowsEndpoint bAck,
      std::wstring executable, std::wstring arguments, quint64 revision,
      decltype(&PeekNamedPipe) peek = &PeekNamedPipe);
  qint64 nowMs() const override;
  bool ownsAChannel(const void* channel) const override;
  bool validateEvidence(const Evidence& evidence) const override;
  Progress internalReplacementStop(qint64 deadline) override;
  Progress waitAClosed(qint64 deadline) override;
  Progress waitAAndChildren(qint64 deadline, bool& forced) override;
  Progress launchB(qint64 deadline) override;
  Progress launchB(const Grant& grant, const QByteArray& digest, qint64 deadline) override;
  Progress transferToB(const QByteArray& frame, qint64 deadline) override;
  Progress waitBGrantAccepted(qint64 deadline) override;
  Progress issueRestorePermit(qint64 deadline, const std::function<bool()>& stillCurrent) override;
  Progress monitorB(qint64 deadline) override;
  Progress cancelBAndAwaitAck(qint64 deadline) override;
  Progress cleanupOwned(qint64 deadline) override;
  bool configureHealthyTransfer(const Closed& expected, quint64 bLaunch,
      WindowsRestartChannel* identity, std::shared_ptr<HealthyModuleIdentity::StageLease>& lease,
      std::function<bool(const Grant&)> currentIntent);
  bool eligibleForB(const Grant& grant) const override;
private:
  struct Exchange { int stage = 0; QByteArray request; };
  Progress exchange(Exchange& state, WindowsEndpoint write, WindowsEndpoint read,
                    const Control& control, qint64 deadline);
  Progress write(WindowsEndpoint endpoint, const QByteArray& bytes, int& stage, qint64 deadline);
  Progress read(WindowsEndpoint endpoint, DWORD size, int& stage, qint64 deadline, bool expectEof);
  IoProgress trackedBRead(DWORD size, qint64 deadline);
  IoProgress takeBSettlement();
  bool parseBBytes();
  DWORD nextBReadSize() const;
  WindowsRestartProcess* a_;
  WindowsRestartProcess* b_;
  WindowsEndpoint evidence_, aControl_, aAck_, bGrant_, bControl_, bAck_;
  std::wstring executable_, arguments_;
  quint64 revision_;
  Exchange aStop_, bCancel_;
  int grantStage_ = 0;
  bool bLaunchStarted_ = false, aSettled_ = false, bSettled_ = false;
  bool valid_ = false;
  Closed expectedClosed_;
  GrantAccepted expectedAcceptance_;
  WindowsRestartChannel* bIdentity_ = nullptr;
  std::shared_ptr<HealthyModuleIdentity::StageLease> bLease_;
  std::function<bool(const Grant&)> currentIntent_;
  quint64 bLaunchId_ = 0;
  bool healthyConfigured_ = false, bIdentityBound_ = false, bProcessLaunched_ = false, bIdentityFramePending_ = false;
  bool aClosed_ = false, aProcessSettled_ = false, bAccepted_ = false, bEof_ = false;
  bool cancelReadAmbiguous_ = false;
  bool permitCallActive_ = false;
  bool bReadPending_ = false;
  bool bProtocolFailed_ = false;
  bool cancelStarted_ = false, cancelAck_ = false, cancelTerminal_ = false, cancelInitialReadHandled_ = false;
  std::uint64_t bReadOperation_ = 0;
  QByteArray bReadBytes_;
  int aClosedStage_ = 0, aEofStage_ = 0, bAcceptStage_ = 0, permitStage_ = 0, bMonitorStage_ = 0;
  decltype(&PeekNamedPipe) peek_ = nullptr;
};
}
#endif
