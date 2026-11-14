#include "player/AutomaticRecoveryFrameGate.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)

using Gate = AutomaticRecoveryFrameGate;
static constexpr Gate::Key key{1, 2, 3};

static RecoveryFrameObservationV1 frame()
{
  RecoveryFrameObservationV1 value{};
  value.size = sizeof(value); value.version = RECOVERY_FRAME_OBSERVATION_VERSION;
  value.valid = 1; value.playlist_entry_id = 7; value.discontinuity_id = 8;
  value.source_frame_serial = 11; value.render_serial = 21; value.media_pts = 10.0;
  return value;
}

static bool begin(Gate& gate)
{
  return gate.begin(key, 4, 7, 10000, 9, 10, 0, 15000);
}

static void selectedFrameRequiresItsOwnSubmissionAndSwap()
{
  Gate gate; auto value = frame();
  CHECK(begin(gate));
  CHECK(gate.position(key, 4, 7, 10000, true, 9, 1));
  CHECK(gate.rendered(key, 4, 9, value, 2));
  CHECK(!gate.ready(key, 4, 9, 2));
  CHECK(gate.submitted(key, 4, 9, 21, 3));
  CHECK(!gate.ready(key, 4, 9, 3));
  CHECK(gate.swapped(key, 4, 9, 21, 4));
  CHECK(gate.ready(key, 4, 9, 4));
  CHECK(!gate.ready(key, 4, 10, 4));
  CHECK(!gate.ready(key, 4, 9, 2001));
}

static void identityAndSourceFreshnessAreRequired()
{
  for (int failure = 0; failure != 5; ++failure) {
    Gate gate; auto value = frame();
    CHECK(begin(gate));
    if (failure == 0) value.playlist_entry_id = 6;
    if (failure == 1) value.discontinuity_id = 0;
    if (failure == 2) value.source_frame_serial = 10;
    if (failure == 3) value.valid = 0;
    CHECK(!gate.rendered(key, 4, failure == 4 ? 10 : 9, value, 1));
    CHECK(!gate.ready(key, 4, 9, 2));
  }
}

static void firstNativeDiscontinuityIsLatched()
{
  Gate gate; auto value = frame();
  CHECK(begin(gate));
  CHECK(gate.rendered(key, 4, 9, value, 1));
  value.discontinuity_id = 9;
  value.source_frame_serial = 12;
  value.render_serial = 22;
  CHECK(!gate.rendered(key, 4, 9, value, 2));
  CHECK(!gate.ready(key, 4, 9, 2));
}

static void unrelatedOrEarlySwapCannotComplete()
{
  for (int failure = 0; failure != 3; ++failure) {
    Gate gate; auto value = frame();
    CHECK(begin(gate));
    CHECK(gate.position(key, 4, 7, 10000, true, 9, 1));
    CHECK(gate.rendered(key, 4, 9, value, 2));
    if (failure == 0) CHECK(!gate.swapped(key, 4, 9, 21, 3));
    if (failure == 1) CHECK(!gate.submitted(key, 4, 9, 22, 3));
    if (failure == 2) {
      CHECK(gate.submitted(key, 4, 9, 21, 3));
      CHECK(!gate.swapped(key, 4, 9, 22, 4));
    }
    CHECK(!gate.ready(key, 4, 9, 5));
  }
}

static void positionMustBePausedAndWithinTolerance()
{
  Gate unpaused; CHECK(begin(unpaused));
  CHECK(!unpaused.position(key, 4, 7, 10000, false, 9, 1));
  Gate wrongEntry; CHECK(begin(wrongEntry));
  CHECK(!wrongEntry.position(key, 4, 8, 10000, true, 9, 1));
  Gate tooFar; CHECK(begin(tooFar));
  CHECK(!tooFar.position(key, 4, 7, 12001, true, 9, 1));
  Gate boundary; CHECK(begin(boundary));
  CHECK(boundary.position(key, 4, 7, 12000, true, 9, 1));
}

int main()
{
  selectedFrameRequiresItsOwnSubmissionAndSwap();
  identityAndSourceFreshnessAreRequired();
  firstNativeDiscontinuityIsLatched();
  unrelatedOrEarlySwapCannotComplete();
  positionMustBePausedAndWithinTolerance();
}
