#include "healthy_restart/lifetime.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyRetirement;

struct Fixture {
  std::atomic<std::int64_t> now{1};
  Identity identity{11, 22, 33};
  int context = 0;
  std::atomic<int> unregisters{0}, frees{0}, clears{0};
  Operations operations()
  {
    return {[this] { return now.load(); }, [this] { return identity; },
      [this](void*) { ++unregisters; }, [] { return BarrierResult::Ready; },
      [](std::uint64_t) { return true; }, [this](void*) { ++frees; },
      [this](void*) { ++clears; return true; }};
  }
};

struct Gate {
  std::mutex mutex;
  std::condition_variable changed;
  bool entered = false, released = false;
  void waitInside()
  {
    std::unique_lock<std::mutex> lock(mutex);
    entered = true; changed.notify_all();
    changed.wait(lock, [&] { return released; });
  }
  bool waitEntered()
  {
    std::unique_lock<std::mutex> lock(mutex);
    return changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; });
  }
  void release()
  {
    std::lock_guard<std::mutex> lock(mutex);
    released = true; changed.notify_all();
  }
};

static void callbackAndRenderClosure()
{
  Fixture f;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  CHECK(owner.enterCallback(7));
  CHECK(owner.enterRender(7));
  owner.request(7);
  CHECK(!owner.enterCallback(7) && !owner.enterRender(7) && !owner.deliveryAllowed(7));
  CHECK(owner.retireOnRenderThread(7) != Result::Done);
  CHECK(f.frees == 0 && !owner.ack(7));
  owner.leaveCallback();
  owner.leaveRender();
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

static void blockedFreeAndInvalidation()
{
  Fixture f;
  Gate gate;
  auto ops = f.operations();
  ops.freeContext = [&](void*) { ++f.frees; gate.waitInside(); };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  std::thread render([&] { owner.retireOnRenderThread(7); });
  const bool entered = gate.waitEntered();
  if (!entered) { gate.release(); render.join(); CHECK(entered); }
  CHECK(f.frees == 1);
  f.now = 100;
  std::mutex doneMutex;
  std::condition_variable doneChanged;
  bool done = false;
  std::thread invalidator([&] {
    owner.invalidate(7);
    std::lock_guard<std::mutex> lock(doneMutex);
    done = true; doneChanged.notify_all();
  });
  bool didNotBlock;
  {
    std::unique_lock<std::mutex> lock(doneMutex);
    didNotBlock = doneChanged.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
  }
  gate.release();
  render.join();
  invalidator.join();
  CHECK(didNotBlock);
  const auto facts = owner.facts();
  CHECK(f.frees == 1 && facts.freeInvoked && facts.freeReturned);
  CHECK(!owner.ack(7) && !owner.mayDestroy(7, true));
  owner.retireOnRenderThread(7);
  CHECK(f.frees == 1);
}

static void invalidationAndStaleGeneration()
{
  Fixture f;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  CHECK(owner.request(8) != Result::Done);
  CHECK(owner.enterRender(7));
  owner.leaveRender();
  owner.invalidate(7);
  CHECK(owner.retireOnRenderThread(7) != Result::Done);
  CHECK(!owner.ack(7) && !owner.ack(8) && !owner.mayDestroy(7, true));
  CHECK(f.unregisters == 0 && f.frees == 0);
}

static void freeReturnsAfterDeadline()
{
  Fixture f;
  Gate gate;
  auto ops = f.operations();
  ops.freeContext = [&](void*) { ++f.frees; gate.waitInside(); };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  std::thread render([&] { owner.retireOnRenderThread(7); });
  const bool entered = gate.waitEntered();
  if (!entered) { gate.release(); render.join(); CHECK(entered); }
  f.now = 100;
  gate.release();
  render.join();
  CHECK(owner.facts().freeReturned && f.frees == 1);
  CHECK(!owner.ack(7) && !owner.mayDestroy(7, true));
  CHECK(owner.retireOnRenderThread(7) != Result::Done && f.frees == 1);
}

static void invalidationDuringUnregister()
{
  Fixture f;
  Gate gate;
  auto ops = f.operations();
  ops.unregisterCallback = [&](void*) { ++f.unregisters; gate.waitInside(); };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  std::thread render([&] { owner.retireOnRenderThread(7); });
  const bool entered = gate.waitEntered();
  if (!entered) { gate.release(); render.join(); CHECK(entered); }
  std::mutex doneMutex;
  std::condition_variable doneChanged;
  bool done = false;
  std::thread invalidator([&] {
    owner.invalidate(7);
    std::lock_guard<std::mutex> lock(doneMutex);
    done = true; doneChanged.notify_all();
  });
  bool didNotBlock;
  {
    std::unique_lock<std::mutex> lock(doneMutex);
    didNotBlock = doneChanged.wait_for(lock, std::chrono::seconds(2), [&] { return done; });
  }
  gate.release();
  render.join(); invalidator.join();
  CHECK(didNotBlock && f.unregisters == 1 && f.frees == 0);
  CHECK(owner.facts().unregisterReturned && !owner.ack(7) && !owner.mayDestroy(7, true));
}

static void admittedRenderRacesRetirement()
{
  Fixture f;
  Gate gate;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  std::atomic<bool> admitted{false};
  std::thread rendering([&] {
    admitted = owner.enterRender(7);
    gate.waitInside();
    if (admitted) owner.leaveRender();
  });
  const bool entered = gate.waitEntered();
  if (!entered) { gate.release(); rendering.join(); CHECK(entered); }
  CHECK(admitted);
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) == Result::Pending);
  CHECK(f.unregisters == 0 && f.frees == 0 && !owner.ack(7));
  gate.release();
  rendering.join();
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

static void callbackHeldBeforeEntry()
{
  Fixture f;
  Gate gate;
  std::atomic<bool> barrierReady{false}, admitted{true};
  auto ops = f.operations();
  ops.barrier = [&] { return barrierReady ? BarrierResult::Ready : BarrierResult::Waiting; };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  std::thread callback([&] {
    gate.waitInside();
    admitted = owner.enterCallback(7);
    if (admitted) owner.leaveCallback();
  });
  const bool entered = gate.waitEntered();
  if (!entered) { gate.release(); callback.join(); CHECK(entered); }
  owner.request(7);
  CHECK(owner.facts().callbacks == 0);
  CHECK(owner.retireOnRenderThread(7) == Result::Pending);
  CHECK(f.frees == 0 && !owner.ack(7));
  gate.release();
  callback.join();
  CHECK(!admitted);
  barrierReady = true;
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

static void admittedDeliveryCrossesClosure()
{
  Fixture f;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  CHECK(owner.enterDelivery(7));
  CHECK(owner.facts().deliveries == 1);
  owner.request(7);
  CHECK(!owner.enterDelivery(7));
  CHECK(owner.retireOnRenderThread(7) == Result::Pending);
  CHECK(f.frees == 0 && !owner.ack(7));
  owner.leaveDelivery();
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

int main()
{
  callbackAndRenderClosure();
  blockedFreeAndInvalidation();
  freeReturnsAfterDeadline();
  invalidationAndStaleGeneration();
  invalidationDuringUnregister();
  admittedRenderRacesRetirement();
  callbackHeldBeforeEntry();
  admittedDeliveryCrossesClosure();
}
