#pragma once
#ifdef _WIN32
#include "module_endpoints.h"
#include "failure_cause.h"
#include <QByteArray>
#include <QString>
#include <memory>
#include <functional>

namespace FixtureRestart { class ChildGate; }
namespace HealthyRetirement {
class PreparedGraph;

// Driven only by the existing GUI owner loop; this object owns no event loop.
class ChildSession {
public:
  enum class Progress { Pending, Ready, Failed };
  struct SourceClosed { std::uint64_t generation = 0, core = 0, closure = 0, revision = 0; };
  ChildSession(PreparedGraph& graph, std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints,
               std::uint64_t generation);
#ifdef HEALTHY_CHILD_SESSION_TESTING
  struct TestOperations {
    std::function<bool()> valid;
    std::function<bool(const QString&)> initialize;
    std::function<bool(const QByteArray&, bool, QByteArray&)> grant;
    std::function<bool(const QByteArray&)> validateCanceledGrant;
    std::function<bool(const QByteArray&, QByteArray&)> control;
    std::function<bool(QByteArray&)> pollCancellation;
    std::function<bool()> load, seek, revoke;
    std::function<void()> invalidate;
    std::function<bool(HealthyRestart::FailureCause)> beginFailure;
    std::function<Progress()> pollFailure;
    std::function<bool(const QString&, bool, std::uint64_t&, std::uint64_t&, std::uint64_t&)> sourceLoad;
    std::function<bool(QByteArray&)> sourceFreeze;
    std::function<bool(const QByteArray&)> sourceBeginStop;
    std::function<Progress(QByteArray&)> sourcePollStop;
    std::function<bool()> sourceRetire;
    std::function<Progress()> sourcePollRetire;
    std::function<bool(SourceClosed&)> sourceClosed;
  };
  ChildSession(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints,
               std::uint64_t generation, TestOperations operations);
#endif
  bool beginRestore(const QString& fixturePath);
  Progress pollRestore();
  // Ready means only that this I/O operation settled; it is not wire success.
  Progress settleGrantRead();
  Progress settleAckWrite();
  Progress settleControlRead();
  // Ready means Player cleanup qualified, not renderer or core closure.
  Progress pollFailureCleanup();
  bool beginSource(const QString& fixturePath, bool paused);
  bool freezeSource();
  Progress pollSource();
  bool load();
  bool seek();
  Progress pollPlayback();
  bool grantReadSettled() const { return grantSettled_; }
  void invalidate();
private:
  using Io = FixtureRestart::IoProgress;
  using Role = HealthyModuleIdentity::EndpointRole;
  enum class GrantStage { Header, Body, Eof, Done };
  enum class ControlStage { Header, Body };
  bool valid() const;
  bool fail(HealthyRestart::FailureCause cause = HealthyRestart::FailureCause::OwnershipLost);
  void noteFailure(HealthyRestart::FailureCause cause);
  Io beginRead(Role role, unsigned size);
  Io advance(Role role, Io& progress);
  bool pollGrant();
  bool beginGrantRead(unsigned size);
  bool validateCanceledGrant() const;
  bool pollControl();
  bool beginControlRead(unsigned size);
  bool consumeControlBytes(const std::vector<unsigned char>& bytes, bool diagnosticOnly);
  bool pollAck();
  bool queueAck(const QByteArray& frame, bool grant);
  bool sourceControl();
  bool retained() const;
  bool sourceLoad(const QString&, bool);
  bool sourceFreeze(QByteArray&);
  bool sourceBeginStop(const QByteArray&);
  Progress sourcePollStop(QByteArray&);
  bool sourceRetire();
  Progress sourcePollRetire();
  bool sourceClosed(SourceClosed&);
  bool initialize(const QString& path);
  bool grant(const QByteArray& frame, QByteArray& response);
  bool control(const QByteArray& frame, QByteArray& response);
  bool pollCancellation(QByteArray& response);
  bool dispatch(bool seeking);
  void revokeGraph();
  bool beginFailureCleanup();
  Progress advanceFailureCleanup();
  PreparedGraph* graph_;
  std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints_;
  std::shared_ptr<FixtureRestart::ChildGate> restoreGate_;
  const std::uint64_t generation_;
  GrantStage grantStage_ = GrantStage::Header;
  ControlStage controlStage_ = ControlStage::Header;
  Io grantIo_ = Io::Failed, controlIo_ = Io::Failed, ackIo_ = Io::Failed;
  QByteArray grantFrame_, controlFrame_, ackFrame_;
  std::uint64_t grantOperation_ = 0;
  std::uint64_t controlOperation_ = 0;
  unsigned grantTarget_ = 4;
  unsigned controlTarget_ = 4;
  bool grantCancelStarted_ = false, grantSettled_ = false;
  bool controlContainCancelStarted_ = false, controlSettled_ = false;
  bool grantSettlementFailed_ = false, controlSettlementFailed_ = false;
  bool restoreStarted_ = false;
  bool started_ = false, failed_ = false, grantAck_ = false, ackIsGrant_ = false;
  // Transport routing only; the child gate remains the playback authority.
  bool cancelReceived_ = false, cancelTerminal_ = false;
  bool restorePolling_ = false;
  enum class FailurePhase { None, Starting, Pending, Qualified, Unknown };
  FailurePhase failurePhase_ = FailurePhase::None;
  bool failurePolling_ = false;
  HealthyRestart::FailureCause firstFailure_ = HealthyRestart::FailureCause::None;
  enum class SourceStage { Idle, Evidence, Control, Stop, StopAck, Retirement, ControlCancel, ClosedAck, Done };
  SourceStage sourceStage_ = SourceStage::Idle;
  Io sourceIo_ = Io::Failed;
  QByteArray closedFrame_;
  bool controlCancelStarted_ = false;
  std::uint64_t sourceRun_ = 0, sourceLaunch_ = 0, sourceRevision_ = 0, stopSequence_ = 0;
#ifdef HEALTHY_CHILD_SESSION_TESTING
  TestOperations test_;
  bool testing_ = false;
#endif
};
}
#endif
