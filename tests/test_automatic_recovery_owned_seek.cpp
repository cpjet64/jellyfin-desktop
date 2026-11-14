#include "player/AutomaticRecoveryOwnedSeek.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Seek = AutomaticRecoveryOwnedSeek;
using State = Seek::State;
static constexpr Seek::Key key{1, 2, 3};

static Seek::Operation operation(bool paused = false)
{
  Seek::Operation op;
  op.key = key; op.operationId = 40; op.replyId = (std::uint64_t{3} << 61) | 40;
  op.baselineEpoch = 10; op.baselineDiscontinuity = 20; op.baselineSource = 30;
  op.rendererGeneration = 4; op.entry = 7; op.targetMs = 10000; op.checkpointMs = 3000;
  op.dispatchedMs = 100; op.desiredPaused = paused;
  return op;
}

static Seek::Position position(bool paused = false)
{
  Seek::Position value;
  value.key = key; value.eventEpoch = 11; value.batchSequence = 10; value.entry = 7; value.positionMs = 10000;
  value.observedMs = 200; value.seekable = true; value.video = true; value.nativePaused = paused;
  return value;
}

static Seek::Submission submission()
{
  Seek::Submission value;
  value.key = key; value.operationId = 40; value.entry = 7; value.eventEpoch = 11;
  value.rendererGeneration = 4; value.discontinuity = 21; value.sourceSerial = 31;
  value.renderSerial = 50; value.ptsMs = 10000; value.privateSubmissionVerified = true;
  return value;
}

static void replyAloneDoesNotProveSeek()
{
  Seek seek; const auto op = operation(); CHECK(seek.begin(op));
  seek.reply(op.replyId, 0, 200);
  CHECK(seek.poll(key, 201) == State::Pending);
  CHECK(seek.poll(key, 15100) == State::PostconditionTimeout);
  CHECK(seek.operation().targetMs == 10000 && seek.operation().checkpointMs == 3000);
}

static void replyAndPostconditionDeadlinesShareDispatchOrigin()
{
  Seek missing; const auto op = operation(); CHECK(missing.begin(op));
  CHECK(missing.poll(key, 5099) == State::Pending);
  CHECK(missing.poll(key, 5100) == State::ReplyTimeout);
  Seek late; CHECK(late.begin(op));
  late.reply(op.replyId, 0, 5099);
  CHECK(late.poll(key, 15099) == State::Pending);
  CHECK(late.poll(key, 15100) == State::PostconditionTimeout);
}

static void pausedSeekNeedsMatchingObservedSubmission()
{
  Seek seek; const auto op = operation(true); auto pos = position(true); auto frame = submission();
  CHECK(seek.begin(op));
  seek.reply(op.replyId, 0, 150);
  seek.position(pos, 200);
  CHECK(seek.poll(key, 201) == State::Pending);
  seek.submission(frame, 202);
  CHECK(seek.poll(key, 203) == State::Complete);
  CHECK(seek.operation().desiredPaused);
}

static void wrongOperationDiscontinuityOrUnverifiedFrameCannotComplete()
{
  for (int failure = 0; failure != 5; ++failure) {
    Seek seek; const auto op = operation(); auto frame = submission();
    CHECK(seek.begin(op)); seek.reply(op.replyId, 0, 150); seek.position(position(), 200);
    if (failure == 0) ++frame.operationId;
    if (failure == 1) frame.discontinuity = op.baselineDiscontinuity;
    if (failure == 2) frame.sourceSerial = op.baselineSource;
    if (failure == 3) frame.entry++;
    if (failure == 4) frame.privateSubmissionVerified = false;
    seek.submission(frame, 202);
    CHECK(seek.poll(key, 203) != State::Complete);
  }
}

static void staleReplyCancellationAndNewIntentCannotComplete()
{
  Seek wrongReply; const auto op = operation(); CHECK(wrongReply.begin(op));
  wrongReply.reply(op.replyId + 1, 0, 150);
  wrongReply.position(position(), 200); wrongReply.submission(submission(), 201);
  CHECK(wrongReply.poll(key, 202) != State::Complete);

  for (int change = 0; change != 3; ++change) {
    Seek seek; CHECK(seek.begin(op));
    auto current = key;
    if (change == 0) seek.cancel(); // Stop or destruction.
    if (change == 1) ++current.session; // New item.
    if (change == 2) ++current.revision; // A newer seek or user intent.
    seek.reply(op.replyId, 0, 150); seek.position(position(), 200); seek.submission(submission(), 201);
    CHECK(seek.poll(current, 202) != State::Complete);
  }
}

static void targetNotPriorCheckpointQualifiesPosition()
{
  Seek seek; const auto op = operation(); auto pos = position(); auto frame = submission();
  CHECK(seek.begin(op)); seek.reply(op.replyId, 0, 150);
  pos.positionMs = op.checkpointMs; frame.ptsMs = op.checkpointMs;
  seek.position(pos, 200); seek.submission(frame, 201);
  CHECK(seek.poll(key, 202) != State::Complete);
}

static void newerContradictoryNativeStateRevokesOldPosition()
{
  for (int contradiction = 0; contradiction != 3; ++contradiction) {
    Seek seek; const auto op = operation(); auto newer = position();
    CHECK(seek.begin(op)); seek.reply(op.replyId, 0, 150); seek.position(position(), 200);
    newer.observedMs = 201;
    newer.batchSequence = 11;
    if (contradiction == 0) newer.seeking = true;
    if (contradiction == 1) newer.nativePaused = true;
    if (contradiction == 2) newer.positionMs = op.checkpointMs;
    seek.position(newer, 201);
    seek.submission(submission(), 202);
    CHECK(seek.poll(key, 203) != State::Complete);
    auto fresh = position(); fresh.batchSequence = 12; fresh.observedMs = 204;
    seek.position(fresh, 204);
    CHECK(seek.poll(key, 205) == State::Complete);
  }
}

static void staleOrWrongScopeCannotRevokeFreshPosition()
{
  for (int stale = 0; stale != 5; ++stale) {
    Seek seek; const auto op = operation(); auto old = position();
    CHECK(seek.begin(op)); seek.reply(op.replyId, 0, 150); seek.position(position(), 200);
    old.observedMs = 201; old.seeking = true;
    if (stale == 0) old.batchSequence = 10; // Duplicate.
    if (stale == 1) old.batchSequence = 9;
    if (stale == 2) { old.batchSequence = 11; ++old.key.revision; }
    if (stale == 3) { old.batchSequence = 11; ++old.entry; }
    if (stale == 4) { old.batchSequence = 11; old.eventEpoch = op.baselineEpoch; }
    seek.position(old, 201);
    seek.submission(submission(), 202);
    CHECK(seek.poll(key, 203) == State::Complete);
  }
}

int main()
{
  replyAloneDoesNotProveSeek();
  replyAndPostconditionDeadlinesShareDispatchOrigin();
  pausedSeekNeedsMatchingObservedSubmission();
  wrongOperationDiscontinuityOrUnverifiedFrameCannotComplete();
  staleReplyCancellationAndNewIntentCannotComplete();
  targetNotPriorCheckpointQualifiesPosition();
  newerContradictoryNativeStateRevokesOldPosition();
  staleOrWrongScopeCannotRevokeFreshPosition();
}
