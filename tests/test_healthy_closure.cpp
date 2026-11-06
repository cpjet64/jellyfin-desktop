#include "closure.h"
#include "closure_render.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <optional>
#include <thread>

using namespace HealthyRetirement;
static void require(bool value) { if (!value) std::abort(); }

struct Scenario {
  std::atomic<std::int64_t> now{10};
  std::atomic<int> stage{0}, joins{0}, destroys{0};
  std::atomic<bool> joinResult{true}, wrongThread{false};
  Closure::StopKey expected{Closure::StopKey::Origin::InternalReplacement, 1, 2, 3, 4, 5, 6, 7, true};
  std::optional<Closure::StopKey> observed = expected;
  Closure::EventResult event = Closure::EventResult::Closed;
  QThread* gui = QThread::currentThread();
  std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
  std::shared_ptr<Slot> slot = std::make_shared<Slot>(9, 100, [this] { return now.load(); });

  std::shared_ptr<Lifetime> retired(bool qualify = true) {
    Operations ops;
    ops.now = [this] { return now.load(); };
    ops.currentIdentity = [] { return Identity{1, 2, 3}; };
    ops.unregisterCallback = [](void*) {};
    ops.barrier = [] { return BarrierResult::Ready; };
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [](void*) {};
    ops.clearPointer = [](void*) { return true; };
    auto life = std::make_shared<Lifetime>(9, Identity{1, 2, 3}, this, 100, ops,
                                           qualify ? Barrier::TestQualified : Barrier::Unqualified);
    require(life->bindClaimForTest(claim));
    require(life->request(9) == Result::Pending);
    require(life->retireOnRenderThread(9) == (qualify ? Result::Done : Result::Blocked));
    return life;
  }
  Closure::Operations operations() {
    Closure::Operations ops;
    ops.now = [this] { return now.load(); };
    ops.settledStop = [this] { return observed; };
    ops.closeEvents = [this] { if (event == Closure::EventResult::Closed) stage = 1; return event; };
    ops.quitWorker = [this] { require(stage == 1); stage = 2; };
    ops.joinWorker = [this](std::int64_t left) {
      require(stage == 2 && left == 90); wrongThread = QThread::currentThread() == gui;
      ++joins; stage = 3; return joinResult.load();
    };
    ops.destroyCore = [this] { require(stage == 3); ++destroys; stage = 4; };
    return ops;
  }
};

static void drain(Closure& closure)
{
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < 3000) {
    closure.poll();
    const auto phase = closure.facts().phase;
    if (phase == Closure::Phase::Closed) return;
    QThread::msleep(1);
  }
}

static void successful_order()
{
  Scenario s; auto life = s.retired();
  Closure closure(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(closure.start()); require(!closure.start());
  drain(closure);
  require(closure.closed(9) && !closure.closed(10));
  require(s.stage == 4 && s.joins == 1 && s.destroys == 1 && !s.wrongThread);
  closure.poll(); require(s.destroys == 1);
}

static void unqualified_and_wrong_stop()
{
  Scenario s; auto unqualified = s.retired(false);
  Closure blocked(s.slot, unqualified, 9, 100, s.expected, s.claim, s.operations());
  require(!blocked.start() && s.stage == 0 && !blocked.closed(9));
  auto life = s.retired(); s.observed->request++;
  Closure wrong(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(!wrong.start() && s.stage == 0 && !wrong.closed(9));
  s.observed.reset();
  Closure missing(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(!missing.start() && s.destroys == 0);
}

static void event_and_join_failures()
{
  Scenario s; auto life = s.retired(); s.event = Closure::EventResult::Pending;
  Closure pending(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(pending.start()); pending.poll(); require(s.stage == 0 && !pending.closed(9));
  s.event = Closure::EventResult::Failed; pending.poll();
  require(pending.facts().phase == Closure::Phase::Retained && s.destroys == 0);
  Scenario t; auto other = t.retired(); t.joinResult = false;
  Closure failure(t.slot, other, 9, 100, t.expected, t.claim, t.operations());
  require(failure.start()); drain(failure);
  require(!failure.closed(9) && t.joins == 1 && t.destroys == 0);
}

static void claim_identity_and_revocation()
{
  Scenario s; auto life = s.retired();
  Scenario other; auto otherLife = other.retired();
  require(otherLife->ack(9).has_value());
  Closure wrong(s.slot, life, 9, 100, s.expected, other.claim, s.operations());
  require(!wrong.start() && !wrong.closed(9) && s.destroys == 0);
  Closure valid(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(valid.start()); drain(valid); require(valid.closed(9));
  s.claim->revoke();
  require(!valid.closed(9) && s.destroys == 1);
}

static void superseded_stop_cannot_close()
{
  Scenario s; auto life = s.retired(); s.event = Closure::EventResult::Pending;
  Closure closure(s.slot, life, 9, 100, s.expected, s.claim, s.operations());
  require(closure.start()); closure.poll();
  s.observed->revision++;
  s.event = Closure::EventResult::Closed;
  closure.poll();
  require(!closure.closed(9) && s.joins == 0 && s.destroys == 0 && s.stage < 2);
}

static void late_join_or_invalidation(int mode)
{
  Scenario s; auto life = s.retired();
  std::atomic<bool> entered{false}, release{false};
  auto ops = s.operations();
  ops.joinWorker = [&](std::int64_t) {
    entered = true;
    while (!release.load()) QThread::msleep(1);
    return true;
  };
  Closure closure(s.slot, life, 9, 100, s.expected, s.claim, ops);
  require(closure.start()); closure.poll();
  QElapsedTimer timer; timer.start();
  while (!entered.load() && timer.elapsed() < 3000) QThread::msleep(1);
  require(entered.load());
  if (mode == 1) closure.invalidate();
  else if (mode == 2) life->invalidate(9);
  else if (mode == 3) s.claim->revoke();
  else s.now = 100;
  release = true;
  drain(closure);
  require(!closure.closed(9) && s.destroys == 0);
}

static void late_destroy_cannot_acknowledge()
{
  Scenario s; auto life = s.retired();
  std::atomic<bool> entered{false}, release{false};
  Closure* active = nullptr;
  auto ops = s.operations();
  ops.destroyCore = [&] {
    require(active->facts().phase == Closure::Phase::Destroying);
    ++s.destroys; entered = true;
    while (!release.load()) QThread::msleep(1);
  };
  Closure closure(s.slot, life, 9, 100, s.expected, s.claim, ops); active = &closure;
  require(closure.start()); closure.poll();
  QElapsedTimer timer; timer.start();
  while (!entered.load() && timer.elapsed() < 3000) QThread::msleep(1);
  require(entered.load());
  s.now = 100; release = true;
  drain(closure);
  require(!closure.closed(9) && s.destroys == 1);
  closure.poll(); require(s.destroys == 1);
}

static void item_loss_during_render_free()
{
  auto render = std::make_shared<RenderContextState>();
  auto* context = reinterpret_cast<mpv_render_context*>(static_cast<std::uintptr_t>(1));
  render->context.store(context);
  std::atomic<bool> entered{false}, release{false};
  Operations ops;
  ops.now = [] { return std::int64_t{10}; };
  ops.currentIdentity = [] { return Identity{1, 2, 3}; };
  ops.unregisterCallback = [](void*) {};
  ops.barrier = [] { return BarrierResult::Ready; };
  ops.invalidateQueued = [](std::uint64_t) { return true; };
  ops.freeContext = [&](void*) {
    entered = true;
    while (!release.load()) std::this_thread::yield();
  };
  ops.clearPointer = [render](void* value) {
    return render->clear(static_cast<mpv_render_context*>(value));
  };
  auto life = std::make_shared<Lifetime>(9, Identity{1, 2, 3}, context, 100, ops, Barrier::TestQualified);
  auto claim = std::make_shared<RetirementClaim>(9);
  require(life->bindClaimForTest(claim));
  require(life->request(9) == Result::Pending);
  std::thread thread([&] { require(life->retireOnRenderThread(9) == Result::Blocked); });
  QElapsedTimer timer; timer.start();
  while (!entered.load() && timer.elapsed() < 3000) QThread::msleep(1);
  require(entered.load());
  life->invalidate(9); release = true; thread.join();
  require(!life->ack(9) && render->context.load() == nullptr);
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  successful_order(); unqualified_and_wrong_stop(); event_and_join_failures(); claim_identity_and_revocation();
  superseded_stop_cannot_close();
  late_join_or_invalidation(0); late_join_or_invalidation(1);
  late_join_or_invalidation(2); late_join_or_invalidation(3);
  late_destroy_cannot_acknowledge();
  item_loss_during_render_free();
}
