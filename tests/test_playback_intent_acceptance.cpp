#include "player/PlaybackIntent.h"
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using Intent = PlaybackIntent;

static void stopWinsOverLateCompletion()
{
  Intent state(9);
  state.play(false);
  const auto play = state.snapshot();
  state.stop(Intent::Reason::NativeStop);
  const auto stopped = state.snapshot();
  CHECK(stopped.desired == Intent::Desired::Stopped && stopped.revision > play.revision);
  CHECK(!state.dispatch(play.revision, Intent::Dispatch::NativeAccepted));
  CHECK(state.snapshot().desired == Intent::Desired::Stopped);
  CHECK(state.dispatch(stopped.revision, Intent::Dispatch::Unavailable));
  CHECK(state.snapshot().dispatch == Intent::Dispatch::Unavailable && !state.automaticResumeEligible());
}

static void newIntentCannotBePoisonedByOldCompletion()
{
  Intent state(9);
  state.play(false);
  const auto old = state.snapshot();
  state.play(true);
  const auto current = state.snapshot();
  CHECK(current.request > old.request && current.desired == Intent::Desired::Paused);
  CHECK(!state.dispatch(old.revision, Intent::Dispatch::NativeRejected));
  CHECK(state.snapshot().request == current.request && state.snapshot().desired == Intent::Desired::Paused);
  state.seek(12000);
  const auto seek = state.snapshot();
  CHECK(seek.hasSeek && seek.seekMs == 12000 && !seek.timelineQualified);
  CHECK(!state.dispatch(current.revision, Intent::Dispatch::NativeAccepted));
  CHECK(!state.automaticResumeEligible());
}

static void unattributedAndExitRemainIneligible()
{
  Intent state(9);
  state.play(false);
  const auto request = state.snapshot().request;
  state.invalidate(Intent::Reason::UnattributedEvent);
  CHECK(state.snapshot().request == request && !state.snapshot().timelineQualified);
  CHECK(state.snapshot().observation == Intent::Observation::Unknown);
  state.exit();
  CHECK(state.snapshot().desired == Intent::Desired::Exiting && !state.automaticResumeEligible());
  Intent invalid(0);
  invalid.play(false);
  CHECK(invalid.snapshot().reason == Intent::Reason::CounterExhausted && !invalid.automaticResumeEligible());
  CHECK(!invalid.dispatch(0, Intent::Dispatch::NativeAccepted));
  invalid.exit();
  CHECK(invalid.snapshot().desired == Intent::Desired::Exiting);
  CHECK(!invalid.dispatch(invalid.snapshot().revision, Intent::Dispatch::NativeAccepted));
}

int main()
{
  stopWinsOverLateCompletion();
  newIntentCannotBePoisonedByOldCompletion();
  unattributedAndExitRemainIneligible();
}
