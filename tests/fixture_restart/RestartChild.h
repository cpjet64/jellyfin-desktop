#pragma once
#include "RestartWire.h"
#include <functional>

namespace FixtureRestart {
// One serialized fixture-owner event loop calls these methods. Channel tokens
// come from the retained process/channel allocation, not message contents.
class RestartChild {
public:
  struct ArmIdentity { quint64 host = 0, core = 0, intentRevision = 0, attempt = 0; };
  struct ArmResult {
    enum class Status { Armed, NotArmed, Unknown } status = Status::Unknown;
    ArmIdentity identity;
  };
  struct CleanupResult {
    enum class Status { Pending, NoLoad, Stopped, Failed } status = Status::Failed;
    ArmIdentity identity;
    CancelFailure reason = CancelFailure::None;
  };
  struct Actions {
    std::function<bool()> arm, cancel, load, seek;
    std::function<qint64()> now;
    std::function<ArmResult()> armActual;
    std::function<bool(quint64, quint64)> beginCleanup;
    std::function<CleanupResult()> pollCleanup;
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
  bool pollCancellation(QByteArray& response);
  bool revoke();
  bool load();
  bool seek();
private:
  bool allowed() const;
  enum class ArmPhase { Unstarted, InFlight, Armed, NotArmed, Unknown };
  enum class CancelPhase { None, AwaitArm, Starting, Cleanup, Polling, Done, Failed };
  bool beginCancellation(const Control& control);
  bool startCleanup();
  ChildGate& gate_;
  const void* grantChannel_;
  const void* controlChannel_;
  Grant binding_;
  quint64 launch_ = 0;
  QByteArray expectedDigest_;
  Actions actions_;
  quint64 lastSequence_ = 0, currentRevision_ = 0;
  bool granted_ = false;
  ArmPhase armPhase_ = ArmPhase::Unstarted;
  CancelPhase cancelPhase_ = CancelPhase::None;
  ArmIdentity identity_;
  quint64 cancelSequence_ = 0, cancelRevision_ = 0;
  bool protocolFailed_ = false;
  bool cleanupStartFailed_ = false;
};
}
