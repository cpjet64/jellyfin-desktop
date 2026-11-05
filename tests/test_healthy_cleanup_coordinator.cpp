#include "healthy_cleanup_coordinator.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyCleanup;

// The injected operations represent native barriers; these tests do not prove them.
struct Fake : Operations {
  std::vector<Step> calls;
  Step failure = Step::None;
  Step expiresAt = Step::None;
  std::int64_t time = 1;
  int stops = 0;
  Stop stop = {StopStatus::Accepted, 0};
  Identity expected = {7, 8, 9};
  std::int64_t now() noexcept override { return time; }
  Stop cleanupStop(std::int64_t) noexcept override { ++stops; return stop; }
  void freeRender(const Identity& identity, std::int64_t deadline) noexcept override {
    (void)perform(Step::FreeRender, identity, deadline);
  }
  bool perform(Step step, const Identity& identity, std::int64_t) noexcept override {
    calls.push_back(step);
    CHECK(identity.generation == expected.generation && identity.thread == expected.thread &&
          identity.context == expected.context);
    if (step == expiresAt) time = 100;
    return step != failure;
  }
};

struct BarrierFake final : Fake {
  bool admission = true, heldBeforeEntry = false, admitted = false, queued = false, staleRejected = false;
  bool perform(Step step, const Identity& identity, std::int64_t deadline) noexcept override {
    if (!Fake::perform(step, identity, deadline)) return false;
    if (step == Step::CloseRenderAdmission) admission = false;
    if (step == Step::DrainRenderCallbacks && (heldBeforeEntry || admitted)) return false;
    if (step == Step::RejectQueuedRender && queued) staleRejected = !admission;
    return true;
  }
};

static Input input(Context context = Context::Present) {
  Input value{};
  value.originalVerdict = true;
  value.scenarioStop = {StopStatus::Accepted, 0};
  value.context = context;
  value.identity = {7, 8, 9};
  value.deadline = 100;
  return value;
}

static bool saw(const Fake& fake, Step step) {
  return std::find(fake.calls.begin(), fake.calls.end(), step) != fake.calls.end();
}

static bool before(const Fake& fake, Step first, Step second) {
  const auto a = std::find(fake.calls.begin(), fake.calls.end(), first);
  const auto b = std::find(fake.calls.begin(), fake.calls.end(), second);
  return a != fake.calls.end() && b != fake.calls.end() && a < b;
}

static void successAndNoContext() {
  for (auto context : {Context::Present, Context::Never}) {
    Fake fake;
    Coordinator coordinator;
    auto value = input(context);
    const auto report = coordinator.run(value, fake);
    CHECK(report.entered && report.completed && !report.retained && report.originalVerdict);
    CHECK(saw(fake, Step::DestroyOwned) && saw(fake, Step::FenceWorker));
    CHECK(saw(fake, Step::FreeRender) == (context == Context::Present));
    CHECK(saw(fake, Step::AwaitRenderAck) == (context == Context::Present));
    CHECK(saw(fake, Step::ValidateNoContext) == (context == Context::Never));
    CHECK(before(fake, Step::CloseWakeAdmission, Step::UnregisterWake));
    CHECK(before(fake, Step::UnregisterWake, Step::DrainWakeCallbacks));
    CHECK(before(fake, Step::InvalidatePlayerEvents, Step::FenceWorker));
    CHECK(before(fake, Step::InvalidateHooks, Step::FenceWorker));
    CHECK(before(fake, Step::FenceWorker, Step::DestroyOwned));
    if (context == Context::Present) {
      CHECK(before(fake, Step::CloseRenderAdmission, Step::UnregisterRender));
      CHECK(before(fake, Step::UnregisterRender, Step::DrainRenderCallbacks));
      CHECK(before(fake, Step::DrainRenderCallbacks, Step::FreeRender));
      CHECK(before(fake, Step::RejectQueuedRender, Step::FreeRender));
      CHECK(before(fake, Step::FreeRender, Step::ClearRenderPointer));
      CHECK(before(fake, Step::ClearRenderPointer, Step::AwaitRenderAck));
      CHECK(before(fake, Step::AwaitRenderAck, Step::DestroyOwned));
    }
    const auto second = coordinator.run(value, fake);
    CHECK(second.repeatRejected && second.entered);
  }
}

static void originalFailureAndStopEvidence() {
  for (auto phase : {0, 1, 2}) {
    Fake fake;
    Coordinator coordinator;
    auto value = input();
    value.originalVerdict = false;
    value.cleanupStopRequired = phase < 2;
    value.scenarioStop = phase == 2 ? Stop{StopStatus::Accepted, 0} : Stop{StopStatus::NotObserved, 0};
    auto report = coordinator.run(value, fake);
    CHECK(report.completed && !report.originalVerdict);
    CHECK(report.cleanupStopAttempted == (phase < 2));
    CHECK(fake.stops == (phase < 2 ? 1 : 0));
    CHECK(report.scenarioStop.status == value.scenarioStop.status);
  }
}

static void failedStepsNeverRetryOrDestroy() {
  const Step steps[] = {Step::Terminal, Step::CloseRenderAdmission, Step::ValidateRenderContext,
    Step::UnregisterRender, Step::DrainRenderCallbacks, Step::RejectQueuedRender,
    Step::ClearRenderPointer, Step::AwaitRenderAck, Step::CloseWakeAdmission, Step::UnregisterWake,
    Step::DrainWakeCallbacks, Step::InvalidatePlayerEvents, Step::InvalidateHooks, Step::FenceWorker,
    Step::CloseSecondaryObserver, Step::DestroyOwned};
  for (auto step : steps) {
    Fake fake;
    fake.failure = step;
    Coordinator coordinator;
    auto value = input();
    value.originalVerdict = false;
    const auto report = coordinator.run(value, fake);
    CHECK(!report.completed && !report.originalVerdict && report.failed == step);
    CHECK(step == Step::DestroyOwned ? report.ownershipUncertain : report.retained);
    CHECK(std::count(fake.calls.begin(), fake.calls.end(), step) == 1);
    if (step != Step::DestroyOwned) CHECK(!saw(fake, Step::DestroyOwned));
    const auto size = fake.calls.size();
    CHECK(coordinator.run(value, fake).repeatRejected && fake.calls.size() == size);
  }
}

static void unknownContextAndDeadline() {
  for (int scenario : {0, 1}) {
    Fake fake;
    Coordinator coordinator;
    auto value = input(scenario ? Context::Unknown : Context::Present);
    if (!scenario) fake.time = value.deadline;
    const auto report = coordinator.run(value, fake);
    CHECK(!report.completed && report.retained);
    CHECK(!saw(fake, Step::FreeRender) && !saw(fake, Step::AwaitRenderAck));
    CHECK(!saw(fake, Step::DestroyOwned));
  }
}

static void stopFailureAndInvalidIdentity() {
  for (auto status : {StopStatus::GuardRejected, StopStatus::Unavailable, StopStatus::Error}) {
    Fake fake;
    fake.stop = {status, -17};
    Coordinator coordinator;
    auto value = input();
    value.originalVerdict = false;
    value.cleanupStopRequired = true;
    const auto report = coordinator.run(value, fake);
    CHECK(report.cleanupStopAttempted && report.cleanupStop.status == status && report.cleanupStop.error == -17);
    CHECK(report.failed == Step::CleanupStop && report.retained && !report.originalVerdict);
    CHECK(fake.stops == 1 && fake.calls.empty());
  }
  Fake fake;
  Coordinator coordinator;
  auto value = input();
  value.identity.context = 0;
  fake.expected = value.identity;
  const auto report = coordinator.run(value, fake);
  CHECK(report.failed == Step::ValidateIdentity && report.retained && !report.freeAttempted);
  CHECK(!saw(fake, Step::DestroyOwned));
}

static void barrierFailuresAndLateCalls() {
  // These injected failures stand for held callbacks, rejected stale work, a
  // wrong context, a missing matching-job ACK, and bounded-event overflow.
  for (auto step : {Step::DrainRenderCallbacks, Step::RejectQueuedRender,
                    Step::ValidateRenderContext, Step::AwaitRenderAck,
                    Step::InvalidatePlayerEvents, Step::InvalidateHooks, Step::FenceWorker}) {
    Fake fake;
    fake.failure = step;
    Coordinator coordinator;
    const auto report = coordinator.run(input(), fake);
    CHECK(report.failed == step && report.retained && !report.completed);
    CHECK(!saw(fake, Step::DestroyOwned));
  }
  for (auto step : {Step::FreeRender, Step::DestroyOwned}) {
    Fake fake;
    fake.expiresAt = step;
    Coordinator coordinator;
    const auto report = coordinator.run(input(), fake);
    CHECK(report.failed == step && !report.completed);
    CHECK(step == Step::DestroyOwned ? report.ownershipUncertain : report.retained);
    if (step == Step::FreeRender) CHECK(report.freeAttempted && report.freeReturned && !report.pointerCleared);
    CHECK(step == Step::DestroyOwned || !saw(fake, Step::DestroyOwned));
  }
  Fake fake;
  fake.failure = Step::ValidateNoContext;
  Coordinator coordinator;
  const auto report = coordinator.run(input(Context::Never), fake);
  CHECK(report.failed == Step::ValidateNoContext && report.retained);
  CHECK(!report.freeAttempted && !saw(fake, Step::AwaitRenderAck));
}

static void injectedCallbackBarriers() {
  // Models the adapter's required admission barrier; native delivery is unproved.
  for (int mode : {0, 1}) {
    BarrierFake fake;
    fake.heldBeforeEntry = mode == 0;
    fake.admitted = mode == 1;
    Coordinator coordinator;
    const auto report = coordinator.run(input(), fake);
    CHECK(report.failed == Step::DrainRenderCallbacks && report.retained);
    CHECK(!fake.admission && !report.freeAttempted && !saw(fake, Step::DestroyOwned));
  }
  BarrierFake fake;
  fake.queued = true;
  Coordinator coordinator;
  const auto report = coordinator.run(input(), fake);
  CHECK(report.completed && fake.staleRejected);
  CHECK(before(fake, Step::RejectQueuedRender, Step::FreeRender));
  CHECK(before(fake, Step::InvalidateHooks, Step::FenceWorker));
  CHECK(before(fake, Step::InvalidatePlayerEvents, Step::FenceWorker));
}

static void wrapperPreservesScenarioFailures() {
  for (int phase : {0, 1, 2}) {
    for (bool throws : {false, true}) {
      Fake fake;
      Coordinator coordinator;
      auto value = input();
      int calls = 0;
      auto scenario = [&](Input& state) -> bool {
        ++calls;
        state.context = Context::Present;
        state.cleanupStopRequired = phase < 2;
        state.scenarioStop = phase == 2 ? Stop{StopStatus::Accepted, 0} : Stop{StopStatus::NotObserved, 0};
        if (throws) throw phase;
        return false;
      };
      const auto report = runScenario(coordinator, value, fake, scenario);
      CHECK(report.completed && !report.originalVerdict && report.scenarioException == throws);
      CHECK(report.cleanupStopAttempted == (phase < 2));
      CHECK(report.scenarioStop.status == (phase == 2 ? StopStatus::Accepted : StopStatus::NotObserved));
      CHECK(runScenario(coordinator, value, fake, scenario).repeatRejected && calls == 1);
    }
  }
}

static void wrapperKeepsAbsoluteDeadline() {
  Fake fake;
  fake.time = 100;
  Coordinator coordinator;
  auto value = input();
  const auto report = runScenario(coordinator, value, fake, [](Input& state) {
    state.deadline = 1000;
    return true;
  });
  CHECK(report.originalVerdict && !report.completed && report.retained);
  CHECK(report.failed == Step::Terminal && fake.calls.empty());
}

static void reentrantWrapperDoesNotRepeatScenarioOrCleanup() {
  Fake fake;
  Coordinator coordinator;
  auto value = input();
  int outer = 0, inner = 0;
  auto scenario = [&](Input&) {
    ++outer;
    const auto rejected = runScenario(coordinator, value, fake, [&](Input&) { ++inner; return true; });
    CHECK(rejected.repeatRejected && fake.calls.empty());
    return true;
  };
  const auto report = runScenario(coordinator, value, fake, scenario);
  CHECK(report.completed && report.repeatRejected && outer == 1 && inner == 0);
  CHECK(std::count(fake.calls.begin(), fake.calls.end(), Step::DestroyOwned) == 1);
}

int main() {
  successAndNoContext();
  originalFailureAndStopEvidence();
  failedStepsNeverRetryOrDestroy();
  unknownContextAndDeadline();
  stopFailureAndInvalidIdentity();
  barrierFailuresAndLateCalls();
  injectedCallbackBarriers();
  wrapperPreservesScenarioFailures();
  wrapperKeepsAbsoluteDeadline();
  reentrantWrapperDoesNotRepeatScenarioOrCleanup();
}
