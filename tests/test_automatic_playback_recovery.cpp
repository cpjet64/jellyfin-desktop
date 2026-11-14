#include "player/AutomaticRecoveryPolicy.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using namespace AutomaticRecovery;

static Sample sample()
{
  Sample value{};
  value.session = 1;
  value.revision = 1;
  value.generation = 1;
  value.positionMs = 0;
  value.seekable = true;
  value.video = true;
  value.visible = true;
  value.exposed = true;
  value.outstanding = true;
  return value;
}

static void stallRequiresFullEligibleInterval()
{
  Policy policy;
  auto s = sample();
  CHECK(policy.observe(s) == Action::None);
  for (s.nowMs = 500; s.nowMs < 5000; s.nowMs += 500) {
    s.positionMs += 500;
    CHECK(policy.observe(s) == Action::None);
  }
  s.positionMs += 500;
  CHECK(policy.observe(s) == Action::Retire);
  CHECK(policy.attempted());
  CHECK(policy.deadlineMs() == 20000);
}

static void exclusionsResetStallBaseline()
{
  // Each distinct adapter exclusion must reset the pending stall interval.
  for (int exclusion = 0; exclusion != 9; ++exclusion) {
    Policy policy;
    auto s = sample();
    CHECK(policy.observe(s) == Action::None);
    s.nowMs = 4500;
    s.positionMs = 4500;
    CHECK(policy.observe(s) == Action::None);
    s.nowMs = 5000;
    if (exclusion == 0) s.paused = true;
    if (exclusion == 1) s.loading = true;
    if (exclusion == 2) s.seeking = true;
    if (exclusion == 3) s.buffering = true;
    if (exclusion == 4) s.visible = false;
    if (exclusion == 5) s.exposed = false;
    if (exclusion == 6) s.video = false;
    if (exclusion == 7) s.eof = true;
    if (exclusion == 8) s.seekable = false;
    CHECK(policy.observe(s) == Action::None);
    s.paused = s.loading = s.seeking = s.buffering = s.eof = false;
    s.visible = s.exposed = s.video = s.seekable = true;
    s.nowMs = 5500;
    CHECK(policy.observe(s) == Action::None);
    s.nowMs = 10499;
    s.positionMs = 9499;
    CHECK(policy.observe(s) == Action::None);
    s.nowMs = 10500;
    s.positionMs = 9500;
    CHECK(policy.observe(s) == Action::Retire);
  }
}

static void progressAndNoOutstandingResetBaseline()
{
  Policy policy;
  auto s = sample();
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 4000;
  s.positionMs = 1;
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 8000;
  s.outstanding = false;
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 8500;
  s.outstanding = true;
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 13499;
  s.positionMs = 5000;
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 13500;
  s.positionMs = 5001;
  CHECK(policy.observe(s) == Action::Retire);
}

static void oneAttemptPerUserSession()
{
  Policy policy;
  auto s = sample();
  s.operationTimedOut = true;
  CHECK(policy.observe(s) == Action::Reload);
  policy.fail();
  s.nowMs = 1000;
  CHECK(policy.observe(s) == Action::None);
  s.generation = 2;
  CHECK(policy.observe(s) == Action::None);
  s.session = 2;
  CHECK(policy.observe(s) == Action::Reload);
}

static void cancellationAndStaleCompletion()
{
  for (int change = 0; change != 3; ++change) {
    Policy policy;
    auto s = sample();
    s.rendererFault = true;
    CHECK(policy.observe(s) == Action::Retire);
    s.rendererFault = false;
    if (change == 0) ++s.session; // Stop/new item uses a new session.
    if (change == 1) ++s.revision; // Seek or track selection.
    if (change == 2) ++s.generation;
    CHECK(policy.observe(s) == Action::Cancel);
    CHECK(!policy.retired(1, 2, 100));
    CHECK(!policy.frameObserved(2, 42, 11, 200));
    CHECK(!policy.mayUnpause(s));
  }
}

static void restorationRequiresFreshReplacementFrame()
{
  Policy policy;
  auto s = sample();
  s.rendererFault = true;
  CHECK(policy.observe(s) == Action::Retire);
  CHECK(policy.retired(1, 2, 100));
  CHECK(policy.restoreStarted(42, 10, 200));
  CHECK(!policy.mayUnpause(s));
  CHECK(policy.frameObserved(2, 42, 11, 400));
  s.generation = 2;
  s.rendererFault = false;
  s.nowMs = 400;
  CHECK(policy.mayUnpause(s));
  ++s.revision;
  CHECK(!policy.mayUnpause(s));
}

static void pausedRestorationAndDeadlineFailClosed()
{
  Policy policy;
  auto s = sample();
  s.paused = true;
  s.rendererFault = true;
  CHECK(policy.observe(s) == Action::Retire);
  CHECK(policy.retired(1, 2, 100));
  CHECK(policy.restoreStarted(42, 10, 200));
  CHECK(policy.frameObserved(2, 42, 11, 300));
  s.generation = 2;
  s.nowMs = 300;
  CHECK(!policy.mayUnpause(s));

  Policy expired;
  s = sample();
  s.rendererFault = true;
  CHECK(expired.observe(s) == Action::Retire);
  CHECK(!expired.retired(1, 2, 15000));
  CHECK(!expired.restoreStarted(42, 10, 15000));
  CHECK(!expired.frameObserved(2, 42, 11, 15000));
  CHECK(!expired.mayUnpause(s));
}

static void failureNeverAcknowledgesRetirementOrRestore()
{
  for (int stage = 0; stage != 2; ++stage) {
    Policy policy;
    auto s = sample();
    s.rendererFault = true;
    CHECK(policy.observe(s) == Action::Retire);
    if (stage == 1) {
      CHECK(policy.retired(1, 2, 100));
      CHECK(policy.restoreStarted(42, 10, 200));
    }
    policy.fail();
    CHECK(policy.phase() == Phase::Failed);
    CHECK(!policy.retired(1, 2, 300));
    CHECK(!policy.restoreStarted(42, 10, 300));
    CHECK(!policy.frameObserved(2, 42, 11, 300));
    CHECK(!policy.mayUnpause(s));
    CHECK(policy.observe(s) != Action::Retire);
  }
}

static void staleFrameCannotCompleteReplacement()
{
  Policy policy;
  auto s = sample();
  s.rendererFault = true;
  CHECK(policy.observe(s) == Action::Retire);
  CHECK(policy.retired(1, 2, 100));
  CHECK(policy.restoreStarted(42, 10, 200));
  CHECK(!policy.frameObserved(1, 42, 11, 300));
  CHECK(!policy.mayUnpause(s));
  CHECK(policy.phase() == Phase::Failed);
}

static void newFrameAndFrozenClockSuppressStall()
{
  Policy policy;
  auto s = sample();
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 4000;
  s.positionMs = 4000;
  ++s.frameSerial;
  CHECK(policy.observe(s) == Action::None);
  s.nowMs = 9000;
  CHECK(policy.observe(s) == Action::None);
  CHECK(!policy.attempted());
}

static void pausedTimeoutCannotAuthorizePlaying()
{
  Policy policy;
  auto s = sample();
  s.paused = true;
  s.operationTimedOut = true;
  CHECK(policy.observe(s) == Action::Reload);
  policy.fail();
  s.nowMs = 200;
  CHECK(!policy.mayUnpause(s));
}

static void reloadRequiresNewAttributableFrame()
{
  Policy policy;
  auto s = sample();
  s.operationTimedOut = true;
  CHECK(policy.observe(s) == Action::Reload);
  // The caller supplies a newly accepted load after its post-seek state.
  CHECK(policy.restoreStarted(42, 10, 100));
  CHECK(!policy.mayUnpause(s));
  CHECK(policy.frameObserved(1, 42, 11, 200));
  s.nowMs = 200;
  s.operationTimedOut = false;
  CHECK(policy.mayUnpause(s));
}

static void reloadRejectsOldFrameOrWrongAttempt()
{
  for (int variant = 0; variant != 3; ++variant) {
    Policy policy;
    auto s = sample();
    s.operationTimedOut = true;
    CHECK(policy.observe(s) == Action::Reload);
    CHECK(policy.restoreStarted(42, 10, 100));
    const auto attempt = variant == 0 ? 41 : 42;
    const auto serial = variant == 1 ? 10 : variant == 2 ? 9 : 11;
    CHECK(!policy.frameObserved(1, attempt, serial, 200));
    CHECK(policy.phase() == Phase::Failed);
    CHECK(!policy.mayUnpause(s));
  }
}

static void invalidRevisionAndRetrogradeTimeFailClosed()
{
  Policy invalid;
  auto s = sample();
  s.revision = 0;
  s.rendererFault = true;
  CHECK(invalid.observe(s) == Action::Cancel);
  CHECK(!invalid.attempted());

  for (int stage = 0; stage != 3; ++stage) {
    Policy policy;
    s = sample();
    s.nowMs = 100;
    s.rendererFault = true;
    CHECK(policy.observe(s) == Action::Retire);
    if (stage == 0) {
      CHECK(!policy.retired(1, 2, 99));
    } else {
      CHECK(policy.retired(1, 2, 200));
      if (stage == 1) {
        CHECK(!policy.restoreStarted(42, 10, 199));
      } else {
        CHECK(policy.restoreStarted(42, 10, 300));
        CHECK(!policy.frameObserved(2, 42, 11, 299));
      }
    }
    CHECK(policy.phase() == Phase::Failed);
    CHECK(!policy.mayUnpause(s));
  }
}

int main()
{
  stallRequiresFullEligibleInterval();
  exclusionsResetStallBaseline();
  progressAndNoOutstandingResetBaseline();
  oneAttemptPerUserSession();
  cancellationAndStaleCompletion();
  restorationRequiresFreshReplacementFrame();
  pausedRestorationAndDeadlineFailClosed();
  failureNeverAcknowledgesRetirementOrRestore();
  staleFrameCannotCompleteReplacement();
  newFrameAndFrozenClockSuppressStall();
  pausedTimeoutCannotAuthorizePlaying();
  reloadRequiresNewAttributableFrame();
  reloadRejectsOldFrameOrWrongAttempt();
  invalidRevisionAndRetrogradeTimeFailClosed();
}
