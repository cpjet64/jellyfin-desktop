#include "AutomaticRecoveryOwnedSeek.h"
#include <limits>

bool AutomaticRecoveryOwnedSeek::current(Key key) const
{
  return key.session && key.session == operation_.key.session && key.revision == operation_.key.revision &&
      key.generation == operation_.key.generation;
}
bool AutomaticRecoveryOwnedSeek::timely(std::int64_t now) const
{ return state_ == State::Pending && now >= lastMs_ && now < completionDeadline_; }
bool AutomaticRecoveryOwnedSeek::near(std::int64_t position) const
{
  if (position < 0) return false;
  return position >= operation_.targetMs ? position - operation_.targetMs <= 2000 : operation_.targetMs - position <= 2000;
}
bool AutomaticRecoveryOwnedSeek::begin(const Operation& operation)
{
  if (!operation.key.session || !operation.key.revision || !operation.key.generation || !operation.operationId ||
      !operation.replyId || !operation.baselineEpoch || !operation.baselineDiscontinuity || !operation.baselineSource ||
      !operation.rendererGeneration || operation.entry <= 0 || operation.targetMs < 0 || operation.checkpointMs < 0 ||
      operation.dispatchedMs < 0 || operation.dispatchedMs > std::numeric_limits<std::int64_t>::max() - 15000) return false;
  operation_ = operation; replyDeadline_ = operation.dispatchedMs + 5000; completionDeadline_ = operation.dispatchedMs + 15000;
  lastMs_ = operation.dispatchedMs; replyOk_ = positionOk_ = submissionOk_ = false;
  replyError_ = 0; position_ = {}; submission_ = {}; state_ = State::Pending; return true;
}
void AutomaticRecoveryOwnedSeek::reply(std::uint64_t userdata, int error, std::int64_t now)
{
  if (!timely(now) || userdata != operation_.replyId || replyOk_ || now >= replyDeadline_) return;
  lastMs_ = now;
  replyError_ = error;
  if (error < 0) { state_ = State::Rejected; return; }
  replyOk_ = true;
}
void AutomaticRecoveryOwnedSeek::position(const Position& value, std::int64_t now)
{
  if (!timely(now) || !current(value.key) || value.entry != operation_.entry || value.eventEpoch <= operation_.baselineEpoch ||
      !value.batchSequence || value.batchSequence <= position_.batchSequence ||
      value.observedMs < operation_.dispatchedMs || value.observedMs < position_.observedMs ||
      value.observedMs > now || now - value.observedMs >= 2000) return;
  position_ = value;
  positionOk_ = value.seekable && value.video && !value.seeking && !value.buffering &&
      value.nativePaused == operation_.desiredPaused && near(value.positionMs);
  lastMs_ = now;
}
void AutomaticRecoveryOwnedSeek::submission(const Submission& value, std::int64_t now)
{
  if (!timely(now) || !current(value.key) || !value.privateSubmissionVerified || value.operationId != operation_.operationId ||
      value.entry != operation_.entry || value.eventEpoch <= operation_.baselineEpoch ||
      value.rendererGeneration != operation_.rendererGeneration || !value.discontinuity ||
      value.discontinuity == operation_.baselineDiscontinuity || value.sourceSerial <= operation_.baselineSource ||
      !value.renderSerial || !near(value.ptsMs)) return;
  submission_ = value; submissionOk_ = true; lastMs_ = now;
}
AutomaticRecoveryOwnedSeek::State AutomaticRecoveryOwnedSeek::poll(Key key, std::int64_t now)
{
  if (state_ != State::Pending) return state_;
  if (!current(key) || now < lastMs_) { state_ = State::Cancelled; return state_; }
  if (!replyOk_ && now >= replyDeadline_) { state_ = State::ReplyTimeout; return state_; }
  if (now >= completionDeadline_) { state_ = State::PostconditionTimeout; return state_; }
  if (replyOk_ && positionOk_ && submissionOk_ && position_.eventEpoch == submission_.eventEpoch &&
      now >= position_.observedMs && now - position_.observedMs < 2000) state_ = State::Complete;
  lastMs_ = now; return state_;
}
void AutomaticRecoveryOwnedSeek::cancel()
{ if (state_ == State::Pending) state_ = State::Cancelled; }
