#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>
#include <memory>
#include <functional>

namespace FixtureRestart {

struct RestorePermit;

struct Grant {
  quint64 run = 0, grant = 0, revision = 0;
  QByteArray assetHash;
  qint64 positionMs = -1;
  bool desiredPaused = false;
};

struct Evidence {
  QByteArray receipt, observation, checkpoint;
  quint64 sourceAttempt = 0;
  Grant grant;
};

// The platform adapter must bind channel to the held A process and allocate B
// with only its intended inherited channel. The fixture does not infer this
// authority from serialized data or from a PID.
class Operations {
public:
  enum class Progress { Pending, Complete, Failed };
  virtual ~Operations() = default;
  virtual qint64 nowMs() const = 0;
  virtual bool ownsAChannel(const void* channel) const = 0;
  virtual bool validateEvidence(const Evidence& evidence) const = 0;
  // Each step must return immediately; repeated calls poll the same operation,
  // never submit it twice. Pending ownership remains with the adapter.
  virtual Progress internalReplacementStop(qint64 deadline) = 0;
  virtual Progress waitAClosed(qint64) { return Progress::Failed; }
  virtual Progress waitAAndChildren(qint64 deadline, bool& forced) = 0;
  virtual Progress launchB(qint64 deadline) = 0;
  virtual Progress launchB(const Grant&, const QByteArray&, qint64) { return Progress::Failed; }
  virtual Progress transferToB(const QByteArray& frame, qint64 deadline) = 0;
  virtual Progress waitBGrantAccepted(qint64) { return Progress::Failed; }
  virtual Progress issueRestorePermit(qint64, const std::function<bool()>&) { return Progress::Failed; }
  virtual Progress monitorB(qint64) { return Progress::Failed; }
  virtual bool eligibleForB(const Grant&) const { return false; }
  virtual Progress cancelBAndAwaitAck(qint64 deadline) = 0;
  virtual Progress cleanupOwned(qint64 deadline) = 0;
};

QByteArray encodeGrant(const Grant& grant);
bool decodeGrant(const QByteArray& frame, const Grant& expected, Grant& output);
bool decodeCommittedGrant(const QByteArray& frame, quint64 expectedRun, const QByteArray& expectedDigest, Grant& output);
bool validateFixtureEvidence(const Evidence& evidence);

class Broker {
public:
  enum class Cancellation { Local, Pending, Acknowledged, Unconfirmed };
  Broker(Operations& operations, const void* heldChannel, const Grant& parentBinding);
  bool capture(const void* channel, const Evidence& evidence);
  bool startReplacement();
  void step();
  bool completed() const { return phase_ == Phase::Done; }
  bool failed() const { return phase_ == Phase::Failed; }
  bool unconfirmed() const { return phase_ == Phase::Unconfirmed; }
  Cancellation cancel();
  Cancellation cancellation() const { return cancellation_; }
  bool burned() const { return burned_; }
  const Evidence& frozenEvidence() const { return evidence_; }
  qint64 overallDeadline() const { return overall_; }
  qint64 normalDeadline() const { return normal_; }
private:
  bool withinNormal() const;
  enum class Phase { Idle, Stop, Closed, Wait, Launch, Transfer, Accepted, Permit, Monitor, Cleanup, Done, Failed, Unconfirmed };
  void stepCancellation();
  void completePhase();
  Operations& operations_;
  const void* channel_;
  Grant parentBinding_;
  qint64 start_, normal_, overall_;
  Evidence evidence_;
  bool captured_ = false, canceled_ = false, burned_ = false, replacementStarted_ = false;
  bool bLaunchStarted_ = false, permitClaimed_ = false;
  bool dispatchActive_ = false;
  QByteArray frame_;
  QByteArray commitment_;
  Phase phase_ = Phase::Idle;
  Cancellation cancellation_ = Cancellation::Local;
};

// Acknowledgement is returned only after local cancellation is applied and the
// checked stop callback succeeds. Calls are serialized by the fixture runner.
class ChildGate {
public:
  ChildGate(const void* channel, qint64 deadline) : channel_(channel), deadline_(deadline) {}
  bool accept(const void* channel, const QByteArray& frame, const Grant& expected, qint64 now, bool eof);
  bool accept(const void* channel, const QByteArray& frame, const Grant& expected,
              const QByteArray& expectedDigest, qint64 now, bool eof);
  bool accept(const void* channel, const QByteArray& frame, quint64 expectedRun,
              const QByteArray& expectedDigest, qint64 now, bool eof);
  bool cancel(bool (*checkedStop)(void*), void* context);
  bool allowed(quint64 revision) const;
  bool canceled() const { return canceled_; }
  bool matchesVerifiedCancel(quint64 sequence, quint64 revision) const;
  bool installPermit(const QByteArray& frame, const RestorePermit& expected, qint64 now);
  bool allowsRestore(quint64 run, quint64 launch, quint64 grant, quint64 revision,
                     const QByteArray& digest, qint64 now) const;
  const Grant* acceptedGrant() const { return accepted_ && !canceled_ ? &grant_ : nullptr; }
private:
  friend class RestartChild;
  bool cancelVerified(quint64 sequence, quint64 revision);
  const void* channel_;
  qint64 deadline_;
  Grant grant_;
  QByteArray acceptedDigest_;
  std::shared_ptr<const RestorePermit> permit_;
  quint64 cancelSequence_ = 0, cancelRevision_ = 0;
  bool attempted_ = false, accepted_ = false, canceled_ = false;
};
}
