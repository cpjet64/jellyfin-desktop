#include "c2/RenderRetirementState.h"
#include "c2/SyntheticFrameService.h"
#include <iostream>

#define CHECK(value) do { if (!(value)) { std::cerr << "failure at line " << __LINE__ << '\n'; return false; } } while (false)

bool syntheticAndOrdinaryClosed()
{
  RenderRetirementState render(7);
  SyntheticFrameService service(7);
  CHECK(service.arm(7, 100, 1));
  CHECK(render.failOnce(RenderRetirementState::Stage::Render, -18));
  CHECK(!render.accepts(7));
  CHECK(service.activate(7, true, -18, true, 2));
  CHECK(!service.allow(6, true, 3));
  CHECK(!service.requestClose(6, true, 3));
  CHECK(service.allow(7, true, 3));
  CHECK(!render.accepts(7));
  CHECK(!service.retirementAllowed(7));
  CHECK(!service.requestClose(7, false, 4));
  CHECK(service.allow(7, true, 5));
  CHECK(service.requestClose(7, true, 6));
  CHECK(!service.requestClose(7, true, 7));
  CHECK(!service.allow(7, true, 7));
  CHECK(service.close(7, true, 8));
  CHECK(!service.close(7, true, 9));
  CHECK(!service.retirementAllowed(7));
  CHECK(!service.acknowledge(6));
  CHECK(service.acknowledge(7));
  CHECK(!service.acknowledge(7));
  CHECK(service.retirementAllowed(7));
  CHECK(!render.accepts(7));
  return true;
}

bool staleRealAndMissingContext()
{
  SyntheticFrameService stale(4);
  CHECK(!stale.arm(3, 100, 0));
  CHECK(stale.arm(4, 100, 0));
  CHECK(!stale.activate(3, true, -18, true, 1));
  CHECK(!stale.retirementAllowed(4));
  CHECK(!stale.activate(4, false, -18, true, 1));
  CHECK(!stale.allow(4, true, 2));
  CHECK(!stale.retirementAllowed(4));
  SyntheticFrameService noContext(5);
  CHECK(noContext.arm(5, 100, 0));
  CHECK(!noContext.activate(5, true, -18, false, 1));
  CHECK(!noContext.retirementAllowed(5));
  SyntheticFrameService lostContext(12);
  CHECK(lostContext.arm(12, 100, 0));
  CHECK(lostContext.activate(12, true, -18, true, 1));
  CHECK(!lostContext.allow(12, false, 2));
  CHECK(!lostContext.retirementAllowed(12));
  SyntheticFrameService closing(6);
  CHECK(closing.arm(6, 100, 0));
  CHECK(closing.activate(6, true, -18, true, 1));
  CHECK(closing.requestClose(6, true, 2));
  CHECK(!closing.close(6, false, 3));
  CHECK(!closing.retirementAllowed(6));
  return true;
}

bool failedServiceAndDeadline()
{
  SyntheticFrameService service(8);
  CHECK(service.arm(8, 100, 0));
  CHECK(service.activate(8, true, -18, true, 1));
  service.result(8, -1);
  CHECK(!service.allow(8, true, 2));
  CHECK(!service.requestClose(8, true, 2));
  CHECK(!service.retirementAllowed(8));
  SyntheticFrameService expired(9);
  CHECK(!expired.arm(9, 0, 0));
  CHECK(expired.arm(9, 5, 0));
  CHECK(expired.activate(9, true, -18, true, 1));
  CHECK(!expired.allow(9, true, 5));
  CHECK(!expired.retirementAllowed(9));
  SyntheticFrameService lateClose(10);
  CHECK(lateClose.arm(10, 5, 0));
  CHECK(lateClose.activate(10, true, -18, true, 1));
  CHECK(!lateClose.requestClose(10, true, 5));
  CHECK(!lateClose.retirementAllowed(10));
  SyntheticFrameService inFlight(11);
  CHECK(inFlight.arm(11, 100, 0));
  CHECK(inFlight.activate(11, true, -18, true, 1));
  CHECK(inFlight.allow(11, true, 2));
  CHECK(inFlight.requestClose(11, true, 3));
  inFlight.result(11, -1);
  CHECK(!inFlight.close(11, true, 4));
  CHECK(!inFlight.retirementAllowed(11));
  return true;
}

bool callbackPath()
{
  SyntheticFrameService service(20);
  int updates = 0, skips = 0, result = 42;
  std::uint64_t mask = 0;
  auto run = [&](std::uint64_t generation, bool original, std::int64_t now) {
    serviceSyntheticFrame([&] { return service.allow(generation, original, now); },
      [&] { ++updates; return mask; },
      [&] { ++skips; service.result(generation, result); }, 1);
  };
  run(20, true, 0);
  CHECK(updates == 0 && skips == 0);
  CHECK(service.arm(20, 100, 0));
  CHECK(service.activate(20, true, -18, true, 1));
  run(19, true, 2);
  CHECK(updates == 0 && skips == 0);
  run(20, true, 3);
  CHECK(updates == 1 && skips == 0);
  mask = 3;
  result = -7;
  run(20, true, 4);
  CHECK(updates == 2 && skips == 1 && result == -7);
  CHECK(!service.requestClose(20, true, 5));
  CHECK(!service.retirementAllowed(20));
  const auto telemetry = service.telemetry();
  CHECK(telemetry.activationResult && telemetry.activationGeneration == 20);
  CHECK(telemetry.activationSynthetic && telemetry.activationOriginal && telemetry.activationError == -18);
  CHECK(telemetry.activationNow == 1 && telemetry.deadline == 100 && telemetry.allowCount == 4);
  CHECK(telemetry.firstRejection == SyntheticFrameService::Rejection::Phase);
  CHECK(telemetry.lastRejection == SyntheticFrameService::Rejection::Generation);
  return true;
}

bool rejectedCallbackPaths()
{
  for (int scenario = 0; scenario < 2; ++scenario) {
    SyntheticFrameService service(21);
    CHECK(service.arm(21, 10, 0));
    CHECK(service.activate(21, true, -18, true, 1));
    int updates = 0, skips = 0;
    serviceSyntheticFrame([&] { return service.allow(21, scenario != 0, scenario ? 10 : 2); },
      [&] { ++updates; return 1; }, [&] { ++skips; }, 1);
    CHECK(updates == 0 && skips == 0);
    CHECK(!service.retirementAllowed(21));
    CHECK(service.telemetry().lastRejection == (scenario ? SyntheticFrameService::Rejection::Deadline :
                                                SyntheticFrameService::Rejection::Context));
  }
  return true;
}

int main()
{
  return syntheticAndOrdinaryClosed() && staleRealAndMissingContext() && failedServiceAndDeadline() &&
         callbackPath() && rejectedCallbackPaths() ? 0 : 1;
}
