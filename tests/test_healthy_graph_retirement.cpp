#include "graph_retirement.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <optional>
#include <stdexcept>

using namespace HealthyRetirement;
static void check(bool value) { if (!value) std::abort(); }

struct TestGraph final : Graph {
  bool* closed;
  bool* released;
  TestGraph(bool* c, bool* r) : closed(c), released(r) {}
  bool eventWorkerClosed() const override { return *closed; }
  bool approveDestruction() override { return *closed; }
  ~TestGraph() override { *released = true; }
};

struct Scenario {
  std::atomic<std::int64_t> now{10};
  std::atomic<bool> capability{true};
  bool capabilityThrows = false;
  std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
  std::shared_ptr<std::atomic_bool> revoked = std::make_shared<std::atomic_bool>(false);
  std::optional<Closure::StopKey> stop = Closure::StopKey{
      Closure::StopKey::Origin::InternalReplacement, 1, 2, 3, 4, 5, 6, 7, true};
  bool workerClosed = false, released = false, schedulerBound = false;
  int binds = 0, events = 0, joins = 0, destroys = 0;
  std::atomic<int> renderFrees{0};
  std::shared_ptr<RenderJob> job;
  std::shared_ptr<Slot> slot = std::make_shared<Slot>(9, 100, [this] { return now.load(); });
  std::shared_ptr<Containment> containment = std::make_shared<Containment>();
  std::shared_ptr<GraphAccess> access = std::make_shared<GraphAccess>();
  std::shared_ptr<Lifetime> lifetime;
  std::shared_ptr<Owner> owner;
  std::shared_ptr<GraphRetirement> retirement;
  Closure::Operations closureOps;

  Scenario() {
    Operations render;
    render.now = [this] { return now.load(); };
    render.currentIdentity = [] { return Identity{1, 2, 3}; };
    render.unregisterCallback = [](void*) {};
    render.barrier = [] { return BarrierResult::Ready; };
    render.invalidateQueued = [](std::uint64_t) { return true; };
    render.freeContext = [this](void*) { ++renderFrees; };
    render.clearPointer = [](void*) { return true; };
    check(slot->bindModuleForTest([this] { return capability.load(); }, revoked));
    check(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(this));
    lifetime = slot->install(Identity{1, 2, 3}, this, render);
    check(static_cast<bool>(lifetime));
    std::unique_ptr<Graph> graph = std::make_unique<TestGraph>(&workerClosed, &released);
    check(containment->adopt(graph, slot, std::make_shared<int>(1)));
    owner = std::make_shared<Owner>(containment, 100, [this] { return now.load(); },
        [this](std::shared_ptr<RenderJob> value) { job = std::move(value); return schedulerBound; }, lifetime);
    GraphRetirement::Boundaries b;
    b.closure.now = [this] { return now.load(); };
    b.closure.settledStop = [this] { return stop; };
    b.closure.closeEvents = [this] { ++events; return Closure::EventResult::Closed; };
    b.closure.quitWorker = [] {};
    b.closure.joinWorker = [this](std::int64_t) { ++joins; return true; };
    b.closure.destroyCore = [this] { ++destroys; workerClosed = true; };
    closureOps = b.closure;
    b.stopClaim = [this] { return claim; };
    b.bindStopClaim = [this](std::shared_ptr<const RetirementClaim> value) {
      return value == claim && slot->bindStopClaimForTest(std::move(value));
    };
    b.capability = [this](std::uint64_t generation) {
      if (capabilityThrows) throw std::runtime_error("capability unavailable");
      return generation == 9 && capability.load();
    };
    b.bindScheduler = [this] { schedulerBound = true; ++binds; };
    retirement = GraphRetirement::forTest(slot, owner, lifetime, access, 100, std::move(b));
  }
};

static void pollUntilDone(Scenario& s)
{
  QElapsedTimer timer; timer.start();
  while (timer.elapsed() < 3000) {
    const auto result = s.retirement->poll();
    if (result != FixtureGraph::Progress::Pending) return;
    QThread::msleep(1);
  }
}

static void qualified_sequence()
{
  Scenario s;
  s.stop.reset();
  check(!s.retirement->request() && s.binds == 0 && !s.job);
  s.stop = Closure::StopKey{Closure::StopKey::Origin::InternalReplacement, 1, 2, 3, 4, 5, 6, 7, true};
  check(s.retirement->request() && s.binds == 1 && s.job);
  check(s.events == 0 && s.joins == 0 && s.destroys == 0 && !s.released);
  s.job->run(); s.job.reset();
  auto other = std::make_shared<RetirementClaim>(9);
  Closure wrong(s.slot, s.lifetime, 9, 100, *s.stop, other, s.closureOps);
  check(!wrong.start() && s.events == 0);
  auto wrongGeneration = std::make_shared<RetirementClaim>(10);
  Closure stale(s.slot, s.lifetime, 9, 100, *s.stop, wrongGeneration, s.closureOps);
  check(!stale.start() && s.events == 0);
  pollUntilDone(s);
  check(s.events == 1 && s.joins == 1 && s.destroys == 1 && s.released);
  const auto ack = s.retirement->ack();
  check(ack && ack->generation == 9 && ack->coreGeneration == 2 && ack->closureGeneration == 9 &&
        ack->revision == 5 && ack->attempt == 6 && ack->completedAt == 10);
  s.capability = false; check(!s.retirement->ack());
  s.capability = true; s.now = 100; check(!s.retirement->ack());
  s.now = 9; check(!s.retirement->ack());
  s.now = 10; s.claim->revoke(); check(!s.retirement->ack());
  check(ack->generation == 9 && ack->coreGeneration == 2 && ack->closureGeneration == 9 &&
        ack->revision == 5 && ack->attempt == 6 && ack->completedAt == 10);
}

static void missing_job_and_revocation()
{
  Scenario missing; check(missing.retirement->request());
  missing.job.reset();
  check(missing.retirement->poll() == FixtureGraph::Progress::Failed);
  check(!missing.access->closed && missing.owner->retained() && !missing.released);
  Scenario revoked; check(revoked.retirement->request());
  revoked.claim->revoke();
  revoked.job->run();
  check(revoked.retirement->poll() == FixtureGraph::Progress::Failed);
  revoked.job.reset();
  check(!revoked.access->closed && revoked.owner->retained() && !revoked.released && revoked.renderFrees == 0);
  Scenario lost; check(lost.retirement->request());
  lost.containment->markLost();
  check(lost.retirement->poll() == FixtureGraph::Progress::Failed);
  lost.job->run(); lost.job.reset();
  check(!lost.access->closed && lost.owner->retained() && !lost.released);
}

static void invalid_key_and_expired_capability()
{
  Scenario bad; bad.stop->origin = Closure::StopKey::Origin::Invalid;
  check(!bad.retirement->request() && bad.binds == 0 && !bad.job);
  Scenario lost; lost.capability = false;
  check(!lost.retirement->request() && lost.binds == 0 && !lost.job);
  Scenario thrown; thrown.capabilityThrows = true;
  check(!thrown.retirement->request() && thrown.owner->retained() && !thrown.access->closed);
  Scenario expired; expired.now = 100;
  check(!expired.retirement->request() && !expired.access->closed && !expired.released);
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  qualified_sequence(); missing_job_and_revocation(); invalid_key_and_expired_capability();
}
