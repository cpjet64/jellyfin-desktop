#include "player/AutomaticRecoveryReadStall.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Watchdog = AutomaticRecoveryReadStall;
using Result = Watchdog::Result;

static Watchdog::Sample sample()
{
  Watchdog::Sample value;
  value.key = {1, 2, 3}; value.entry = 7; value.eventEpoch = 9;
  value.observedMs = value.nowMs = 0; value.positionMs = 1000;
  value.batchSequence = 1; value.sourceFrameSerial = 10;
  value.seekable = value.video = value.buffering = value.errorAbsent = true;
  value.visible = value.exposed = true;
  return value;
}

static Result next(Watchdog& watchdog, Watchdog::Sample& value, int now)
{
  value.nowMs = value.observedMs = now;
  ++value.batchSequence;
  return watchdog.observe(value);
}

static void thirtySecondsRequiresFreshContinuousBatches()
{
  Watchdog watchdog; auto s = sample();
  CHECK(watchdog.observe(s) == Result::None);
  for (int time = 1000; time < 30000; time += 1000)
    CHECK(next(watchdog, s, time) == Result::None);
  CHECK(next(watchdog, s, 30000) == Result::BufferingStall);
}

static void meaningfulPositionOrSourceProgressRestartsInterval()
{
  for (int kind = 0; kind != 2; ++kind) {
    Watchdog watchdog; auto s = sample();
    CHECK(watchdog.observe(s) == Result::None);
    for (int time = 1000; time <= 29000; time += 1000)
      CHECK(next(watchdog, s, time) == Result::None);
    if (kind == 0) ++s.positionMs; else ++s.sourceFrameSerial;
    CHECK(next(watchdog, s, 30000) == Result::None);
    for (int time = 31000; time < 60000; time += 1000)
      CHECK(next(watchdog, s, time) == Result::None);
    CHECK(next(watchdog, s, 60000) == Result::BufferingStall);
  }
}

static void oldBatchAndMissingObservationsCannotAccumulateTime()
{
  Watchdog repeat; auto s = sample();
  CHECK(repeat.observe(s) == Result::None);
  for (int time = 1000; time <= 31000; time += 1000) {
    s.nowMs = time; // Same actual batch and observed timestamp.
    CHECK(repeat.observe(s) == Result::None);
  }
  Watchdog relabel; s = sample();
  CHECK(relabel.observe(s) == Result::None);
  for (int time = 1000; time <= 31000; time += 1000) {
    s.nowMs = time; ++s.batchSequence; // New label cannot refresh old observation.
    CHECK(relabel.observe(s) == Result::None);
  }
  Watchdog gap; s = sample();
  CHECK(gap.observe(s) == Result::None);
  CHECK(next(gap, s, 30000) == Result::None);
  CHECK(next(gap, s, 31000) == Result::None);
}

static void invalidIdentityOrEpochCannotBridgeIntervals()
{
  for (int change = 0; change != 4; ++change) {
    Watchdog watchdog; auto s = sample();
    CHECK(watchdog.observe(s) == Result::None);
    for (int time = 1000; time <= 29000; time += 1000)
      CHECK(next(watchdog, s, time) == Result::None);
    if (change == 0) ++s.entry;
    if (change == 1) ++s.eventEpoch;
    if (change == 2) ++s.key.session;
    if (change == 3) ++s.key.revision;
    CHECK(next(watchdog, s, 30000) == Result::None);
  }
}

static void pauseEofErrorsAndUnknownAreExcluded()
{
  for (int exclusion = 0; exclusion != 10; ++exclusion) {
    Watchdog watchdog; auto s = sample();
    if (exclusion == 0) s.desiredPaused = true;
    if (exclusion == 1) s.nativePaused = true;
    if (exclusion == 2) s.eof = true;
    if (exclusion == 3) s.permanentFailure = true;
    if (exclusion == 4) s.errorAbsent = false;
    if (exclusion == 5) s.buffering = false;
    if (exclusion == 6) s.key.session = 0; // No current user playback.
    if (exclusion == 7) s.entry = 0;
    if (exclusion == 8) s.seeking = true;
    if (exclusion == 9) s.loading = true;
    CHECK(watchdog.observe(s) == Result::None);
    for (int time = 1000; time <= 31000; time += 1000)
      CHECK(next(watchdog, s, time) == Result::None);
  }
}

static void resetDoesNotReauthorizeOldBatchSequences()
{
  Watchdog watchdog; auto s = sample();
  s.batchSequence = 100;
  CHECK(watchdog.observe(s) == Result::None);
  watchdog.reset();
  ++s.key.session;
  s.batchSequence = 1;
  CHECK(watchdog.observe(s) == Result::None);
  for (int time = 1000; time <= 31000; time += 1000)
    CHECK(next(watchdog, s, time) == Result::None);
  s.batchSequence = 101;
  CHECK(next(watchdog, s, 32000) == Result::None);
}

int main()
{
  thirtySecondsRequiresFreshContinuousBatches();
  meaningfulPositionOrSourceProgressRestartsInterval();
  oldBatchAndMissingObservationsCannotAccumulateTime();
  invalidIdentityOrEpochCannotBridgeIntervals();
  pauseEofErrorsAndUnknownAreExcluded();
  resetDoesNotReauthorizeOldBatchSequences();
}
