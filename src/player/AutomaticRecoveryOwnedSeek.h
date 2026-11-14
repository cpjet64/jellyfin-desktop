#pragma once
#include "AutomaticRecoverySession.h"
#include <cstdint>

// Control-thread policy. Native operation attribution must come from the bridge.
class AutomaticRecoveryOwnedSeek {
public:
  using Key = AutomaticRecoverySession::Key;
  enum class State { Empty, Pending, Complete, ReplyTimeout, PostconditionTimeout, Rejected, Cancelled };
  struct Operation {
    Key key;
    std::uint64_t operationId = 0, replyId = 0, baselineEpoch = 0, baselineDiscontinuity = 0;
    std::uint64_t baselineSource = 0, rendererGeneration = 0;
    std::int64_t entry = 0, targetMs = -1, checkpointMs = -1, dispatchedMs = -1;
    bool desiredPaused = false;
  };
  struct Position {
    Key key;
    std::uint64_t eventEpoch = 0, batchSequence = 0;
    std::int64_t entry = 0, positionMs = -1, observedMs = -1;
    bool seekable = false, video = false, seeking = false, buffering = false, nativePaused = false;
  };
  struct Submission {
    Key key;
    std::uint64_t operationId = 0, eventEpoch = 0, rendererGeneration = 0, discontinuity = 0;
    std::uint64_t sourceSerial = 0, renderSerial = 0;
    std::int64_t entry = 0, ptsMs = -1;
    bool privateSubmissionVerified = false;
  };
  bool begin(const Operation& operation);
  void reply(std::uint64_t userdata, int error, std::int64_t nowMs);
  void position(const Position& position, std::int64_t nowMs);
  void submission(const Submission& submission, std::int64_t nowMs);
  State poll(Key current, std::int64_t nowMs);
  void cancel();
  State state() const { return state_; }
  int replyError() const { return replyError_; }
  const Operation& operation() const { return operation_; }
private:
  bool current(Key key) const;
  bool timely(std::int64_t nowMs) const;
  bool near(std::int64_t position) const;
  Operation operation_;
  Position position_;
  Submission submission_;
  std::int64_t replyDeadline_ = 0, completionDeadline_ = 0, lastMs_ = -1;
  bool replyOk_ = false, positionOk_ = false, submissionOk_ = false;
  int replyError_ = 0;
  State state_ = State::Empty;
};
