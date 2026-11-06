#include "healthy_restart/lifetime.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)
using namespace HealthyRetirement;

struct Fixture {
  std::atomic<std::int64_t> now{1};
  Identity identity{11, 22, 33};
  int context = 0, unregisters = 0, frees = 0, clears = 0, invalidations = 0;
  BarrierResult barrierResult = BarrierResult::Ready;
  Operations operations()
  {
    return {[this] { return now.load(); }, [this] { return identity; },
      [this](void* p) { CHECK(p == &context); ++unregisters; },
      [this] { return barrierResult; },
      [this](std::uint64_t g) { CHECK(g == 7); ++invalidations; return true; },
      [this](void* p) { CHECK(p == &context); ++frees; },
      [this](void* p) { CHECK(p == &context); ++clears; return true; }};
  }
};

static void defaultBarrierIsUnqualified()
{
  Fixture f;
  Slot slot(7, 100, [&] { return f.now.load(); });
  CHECK(slot.beginCreation());
  CHECK(slot.beforeCreate());
  const auto owner = slot.install(f.identity, &f.context, f.operations());
  CHECK(owner && owner == slot.current());
  CHECK(owner->enterCallback(7));
  owner->leaveCallback();
  CHECK(slot.request(7) != Result::Done);
  CHECK(owner->retireOnRenderThread(7) != Result::Done);
  const auto facts = owner->facts();
  CHECK(facts.admissionClosed && !facts.barrierQualified && !facts.freeInvoked);
  CHECK(f.frees == 0 && !owner->ack(7) && !owner->mayDestroy(7, true));
  CHECK(!owner->enterCallback(7) && !owner->enterRender(7) && !owner->deliveryAllowed(7));
}

static void clockBounds()
{
  Fixture f;
  f.now = -1;
  Slot negative(7, 100, [&] { return f.now.load(); });
  CHECK(!negative.beginCreation());
  f.now = 5;
  Slot backwards(7, 100, [&] { return f.now.load(); });
  CHECK(backwards.beginCreation());
  f.now = 4;
  CHECK(!backwards.beforeCreate());
  const auto retained = backwards.install(f.identity, &f.context, f.operations());
  CHECK(retained && !retained->enterRender(7) && !retained->ack(7));
  f.now = 5;
  Slot deadline(7, 100, [&] { return f.now.load(); });
  CHECK(deadline.beginCreation());
  f.now = 100;
  CHECK(!deadline.beforeCreate());
  CHECK(f.frees == 0);
}

static void successfulRetirement()
{
  Fixture f;
  const auto original = f.identity;
  Lifetime owner(7, original, &f.context, 100, f.operations(), Barrier::TestQualified);
  CHECK(owner.enterRender(7));
  owner.leaveRender();
  CHECK(owner.enterRender(7));
  owner.leaveRender();
  CHECK(owner.request(7) != Result::Done);
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  const auto facts = owner.facts();
  CHECK(facts.admissionClosed && facts.unregisterInvoked && facts.unregisterReturned);
  CHECK(facts.barrierQualified && facts.queuedInvalidated && facts.freeInvoked && facts.freeReturned);
  CHECK(facts.pointerCleared && facts.ackPublished && f.unregisters == 1 && f.frees == 1 && f.clears == 1);
  const auto ack = owner.ack(7);
  CHECK(ack && ack->generation == 7 && ack->identity.context == original.context && ack->identity.epoch == original.epoch);
  CHECK(!owner.mayDestroy(7, true) && !owner.mayDestroy(8, true) && !owner.ack(8));
  CHECK(owner.acceptAck(7));
  CHECK(!owner.acceptAck(7));
  CHECK(!owner.mayDestroy(7, false) && owner.mayDestroy(7, true));
  owner.retireOnRenderThread(7);
  CHECK(f.unregisters == 1 && f.frees == 1 && f.clears == 1);
}

static void invalidationDuringCreation()
{
  Fixture f;
  Slot absent(0, 100, [&] { return f.now.load(); });
  CHECK(!absent.beginCreation() && !absent.current());
  Slot missingContext(7, 100, [&] { return f.now.load(); });
  CHECK(missingContext.beginCreation());
  const auto missing = missingContext.install(f.identity, nullptr, f.operations());
  if (missing) {
    CHECK(!missing->enterCallback(7) && !missing->enterRender(7));
    CHECK(!missing->ack(7) && !missing->mayDestroy(7, true));
  }
  CHECK(f.frees == 0);
  Slot slot(7, 100, [&] { return f.now.load(); });
  CHECK(!slot.install(f.identity, &f.context, f.operations()));
  CHECK(slot.beginCreation());
  slot.invalidate(7);
  const auto owner = slot.install(f.identity, &f.context, f.operations());
  CHECK(owner && owner == slot.current());
  CHECK(!owner->enterRender(7) && !owner->enterCallback(7));
  CHECK(owner->retireOnRenderThread(7) != Result::Done);
  CHECK(f.frees == 0 && !owner->ack(7) && !owner->mayDestroy(7, true));
  CHECK(!slot.beginCreation());
  Slot requested(7, 100, [&] { return f.now.load(); });
  CHECK(requested.beginCreation());
  CHECK(requested.request(7) != Result::Done);
  const auto late = requested.install(f.identity, &f.context, f.operations());
  CHECK(late && !late->enterRender(7) && !late->ack(7));
  CHECK(f.frees == 0);
  Slot lateCreation(7, 100, [&] { return f.now.load(); });
  CHECK(lateCreation.beginCreation());
  f.now = 100;
  const auto afterDeadline = lateCreation.install(f.identity, &f.context, f.operations());
  CHECK(afterDeadline && !afterDeadline->enterRender(7) && !afterDeadline->ack(7));
  Slot expired(7, 100, [&] { return f.now.load(); });
  CHECK(!expired.beginCreation() && !expired.current());
}

static void invalidatedPublishedAckIsNotAccepted()
{
  Fixture f;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(owner.ack(7));
  owner.invalidate(7);
  CHECK(!owner.acceptAck(7) && !owner.mayDestroy(7, true));
  CHECK(f.frees == 1);
}

static void identityAndDeadlineFailures()
{
  for (int change = 0; change < 4; ++change) {
    Fixture f;
    const auto original = f.identity;
    Lifetime owner(7, original, &f.context, 100, f.operations(), Barrier::TestQualified);
    CHECK(owner.request(7) != Result::Done);
    if (change == 0) ++f.identity.thread;
    if (change == 1) ++f.identity.context;
    if (change == 2) ++f.identity.epoch;
    if (change == 3) f.now = 100;
    CHECK(owner.retireOnRenderThread(7) != Result::Done);
    CHECK(f.frees == 0 && !owner.ack(7) && !owner.mayDestroy(7, true));
  }
  Fixture reused;
  const auto original = reused.identity;
  Lifetime owner(7, original, &reused.context, 100, reused.operations(), Barrier::TestQualified);
  ++reused.identity.epoch;
  CHECK(!owner.enterRender(7));
  CHECK(!owner.ack(7) && reused.frees == 0);
}

static void barrierAndDeliveryFailures()
{
  for (const auto barrier : {BarrierResult::Waiting, BarrierResult::Rejected}) {
    Fixture f;
    f.barrierResult = barrier;
    Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
    CHECK(owner.request(7) != Result::Done);
    CHECK(owner.retireOnRenderThread(7) != Result::Done);
    CHECK(f.frees == 0 && !owner.ack(7));
    CHECK(!owner.deliveryAllowed(7));
    owner.retireOnRenderThread(7);
    CHECK(f.unregisters <= 1 && f.frees == 0);
  }
  Fixture f;
  auto ops = f.operations();
  ops.invalidateQueued = [](std::uint64_t) { return false; };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) != Result::Done);
  CHECK(!owner.ack(7) && f.frees == 0);
}

static void preEntryBarrierAndQueuedWake()
{
  Fixture f;
  f.barrierResult = BarrierResult::Waiting;
  Lifetime owner(7, f.identity, &f.context, 100, f.operations(), Barrier::TestQualified);
  owner.request(7);
  CHECK(owner.facts().callbacks == 0);
  CHECK(owner.retireOnRenderThread(7) == Result::Pending);
  CHECK(f.unregisters == 1 && f.frees == 0 && !owner.ack(7));
  CHECK(!owner.enterCallback(7) && !owner.deliveryAllowed(7));
  f.barrierResult = BarrierResult::Ready;
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

static void reentrantRetirement()
{
  Fixture f;
  Lifetime* target = nullptr;
  auto ops = f.operations();
  ops.unregisterCallback = [&](void*) {
    ++f.unregisters;
    CHECK(target->retireOnRenderThread(7) != Result::Done);
    CHECK(!target->enterCallback(7));
  };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  target = &owner;
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) == Result::Done);
  CHECK(f.unregisters == 1 && f.frees == 1);
}

static void ambiguousFreeAndPointerClear()
{
  Fixture f;
  auto ops = f.operations();
  ops.freeContext = [&](void*) { ++f.frees; throw 1; };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) != Result::Done);
  CHECK(owner.facts().freeInvoked && !owner.facts().freeReturned);
  CHECK(!owner.ack(7) && !owner.mayDestroy(7, true));
  owner.retireOnRenderThread(7);
  CHECK(f.frees == 1);
  Fixture g;
  auto clearFails = g.operations();
  clearFails.clearPointer = [](void*) { return false; };
  Lifetime other(7, g.identity, &g.context, 100, clearFails, Barrier::TestQualified);
  other.request(7);
  CHECK(other.retireOnRenderThread(7) != Result::Done);
  CHECK(other.facts().freeReturned && !other.facts().pointerCleared && !other.ack(7));
  other.retireOnRenderThread(7);
  CHECK(g.frees == 1);
}

static void identityChangesDuringFree()
{
  Fixture f;
  auto ops = f.operations();
  ops.freeContext = [&](void*) { ++f.frees; ++f.identity.epoch; };
  Lifetime owner(7, f.identity, &f.context, 100, ops, Barrier::TestQualified);
  owner.request(7);
  CHECK(owner.retireOnRenderThread(7) != Result::Done);
  CHECK(f.frees == 1 && owner.facts().freeReturned);
  CHECK(!owner.ack(7) && !owner.mayDestroy(7, true));
}

int main()
{
  defaultBarrierIsUnqualified();
  clockBounds();
  successfulRetirement();
  invalidationDuringCreation();
  identityAndDeadlineFailures();
  barrierAndDeliveryFailures();
  preEntryBarrierAndQueuedWake();
  reentrantRetirement();
  ambiguousFreeAndPointerClear();
  identityChangesDuringFree();
  invalidatedPublishedAckIsNotAccepted();
}
