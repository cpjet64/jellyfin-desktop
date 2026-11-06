#include "graph_access.h"
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace HealthyRetirement;
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "check failed at %d\n", __LINE__); std::abort(); } } while (false)

class TestRetirementClaim {
  static void stop_settlement_orders() {
    RetirementClaim revoked(9); CHECK(revoked.enterCleanup(9)); revoked.contain();
    CHECK(revoked.admitCleanupStop(9) == RetirementClaim::Admission::Rejected);
    CHECK(!revoked.recordStopSubmitted(9) && !revoked.recordStopCompleted(9));
    CHECK(!revoked.enterCleanup(9) && !revoked.open() && !revoked.eligible());
    RetirementClaim admitted(9); CHECK(!admitted.enterCleanup(10)); CHECK(admitted.enterCleanup(9));
    CHECK(!admitted.open() && admitted.eligible());
    CHECK(admitted.admitCleanupStop(9) == RetirementClaim::Admission::New);
    CHECK(admitted.admitCleanupStop(9) == RetirementClaim::Admission::Existing);
    CHECK(!admitted.qualifyCleanup(9));
    CHECK(!admitted.claim(9)); // Admission alone is not submission or completion.
    admitted.contain();
    CHECK(admitted.recordStopCompleted(9)); // Reentrant reply before dispatch returns.
    CHECK(!admitted.claim(9) && !admitted.qualifyCleanup(9));
    CHECK(admitted.recordStopSubmitted(9));
    CHECK(!admitted.recordStopSubmitted(9) && !admitted.recordStopCompleted(9));
    CHECK(!admitted.claim(9) && !admitted.eligible());
    RetirementClaim settled(9); CHECK(settled.enterCleanup(9));
    CHECK(settled.admitCleanupStop(9) == RetirementClaim::Admission::New);
    CHECK(settled.recordStopSubmitted(9) && settled.recordStopCompleted(9));
    CHECK(!settled.claim(9));
    CHECK(settled.qualifyCleanup(9) && settled.qualifyCleanup(9) && settled.claim(9));
    CHECK(!settled.claim(9));
  }
  static void actual_access_transition() {
    auto access = std::make_shared<GraphAccess>();
    access->guiThread = std::this_thread::get_id(); access->guiQtThread = QThread::currentThread();
    auto* graph = reinterpret_cast<FixtureGraph*>(static_cast<std::uintptr_t>(1));
    access->graph.store(graph);
    auto claim = std::make_shared<RetirementClaim>(9);
    std::atomic_store(&access->intentClaim, claim);
    CHECK(!GraphAccessOps::beginCleanupForTest(access, 10));
    CHECK(GraphAccessOps::beginCleanupForTest(access, 9) == graph);
    CHECK(!GraphAccessOps::graphOnGuiForTest(access) && GraphAccessOps::cleanupGraphForTest(access) == graph);
    CHECK(!claim->open() && claim->eligible());
    CHECK(claim->admitCleanupStop(9) == RetirementClaim::Admission::New);
    std::thread off([&] { GraphAccessOps::invalidateForTest(access); }); off.join();
    CHECK(claim->contained() && !claim->eligible() && !GraphAccessOps::cleanupGraphForTest(access));
    CHECK(claim->recordStopSubmitted(9) && claim->recordStopCompleted(9));
    CHECK(!GraphAccessOps::beginCleanupForTest(access, 9));
  }
  static void recognize_revoked_cleanup() {
    auto access = std::make_shared<GraphAccess>();
    access->guiThread = std::this_thread::get_id(); access->guiQtThread = QThread::currentThread();
    auto* graph = reinterpret_cast<FixtureGraph*>(static_cast<std::uintptr_t>(1));
    access->graph.store(graph);
    auto claim = std::make_shared<RetirementClaim>(9);
    std::atomic_store(&access->intentClaim, claim);
    CHECK(claim->enterCleanup(9)); claim->revoke();
    CHECK(GraphAccessOps::beginCleanupForTest(access, 9) == graph);
    CHECK(!claim->eligible() && !claim->enterCleanup(9) && !claim->qualifyCleanup(9));
    CHECK(claim->admitCleanupStop(9) == RetirementClaim::Admission::Rejected && !claim->claim(9));
    claim->contain();
    CHECK(!GraphAccessOps::beginCleanupForTest(access, 9));
  }
  static void claim_publication_crosses_containment() {
    auto access = std::make_shared<GraphAccess>();
    access->guiThread = std::this_thread::get_id(); access->guiQtThread = QThread::currentThread();
    std::atomic<bool> paused{false}, release{false};
    GraphAccessOps::setContainPauseForTest([&] {
      paused = true; while (!release.load()) std::this_thread::yield();
    });
    std::thread invalidator([&] { GraphAccessOps::invalidateForTest(access); });
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!paused.load() && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
    CHECK(paused.load());
    auto claim = std::make_shared<RetirementClaim>(9);
    std::atomic_store(&access->intentClaim, claim); // Inject the installer's publication at the real boundary.
    CHECK(access->phase.load() == AccessPhase::Normal && !access->revoked.load() && claim->open());
    release = true; invalidator.join();
    GraphAccessOps::setContainPauseForTest({});
    CHECK(access->phase.load() == AccessPhase::Contained && claim->contained() && !claim->eligible());
    CHECK(!claim->enterCleanup(9) && claim->admitCleanupStop(9) == RetirementClaim::Admission::Rejected);
    auto reverse = std::make_shared<GraphAccess>();
    auto earlier = std::make_shared<RetirementClaim>(9);
    std::atomic_store(&reverse->intentClaim, earlier);
    GraphAccessOps::invalidateForTest(reverse);
    CHECK(reverse->phase.load() == AccessPhase::Contained && earlier->contained() && !earlier->eligible());
  }
  static void render_order(bool revokeBefore) {
    auto claim = std::make_shared<RetirementClaim>(9);
    std::atomic<int> frees{0};
    int context = 0;
    Operations ops;
    ops.now = [] { return std::int64_t{10}; };
    ops.currentIdentity = [] { return Identity{1, 2, 3}; };
    ops.unregisterCallback = [](void*) {};
    ops.barrier = [] { return BarrierResult::Ready; };
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [&](void*) { ++frees; if (!revokeBefore) claim->revoke(); };
    ops.clearPointer = [](void*) { return true; };
    auto life = std::make_shared<Lifetime>(9, Identity{1, 2, 3}, &context, 100, ops, Barrier::TestQualified);
    CHECK(life->bindClaimForTest(claim));
    CHECK(claim->enterCleanup(9) && claim->qualifyCleanup(9) && life->request(9) == Result::Pending);
    if (revokeBefore) claim->revoke();
    CHECK(life->retireOnRenderThread(9) == Result::Blocked);
    CHECK(frees == (revokeBefore ? 0 : 1) && !life->ack(9));
    CHECK(life->retireOnRenderThread(9) == Result::Blocked && frees == (revokeBefore ? 0 : 1));
    CHECK(!claim->eligible() && !claim->enterCleanup(9));
  }
  static void renderer_first_then_internal_failure() {
    auto claim = std::make_shared<RetirementClaim>(9);
    int context = 0, frees = 0;
    Operations ops;
    ops.now = [] { return std::int64_t{10}; };
    ops.currentIdentity = [] { return Identity{1, 2, 3}; };
    ops.unregisterCallback = [](void*) {};
    ops.barrier = [] { return BarrierResult::Ready; };
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [&](void*) {
      ++frees; CHECK(claim->committed() && claim->enterCleanup(9));
      CHECK(!claim->open() && claim->eligible());
    };
    ops.clearPointer = [](void*) { return true; };
    auto life = std::make_shared<Lifetime>(9, Identity{1, 2, 3}, &context, 100, ops, Barrier::TestQualified);
    CHECK(life->bindClaimForTest(claim) && life->request(9) == Result::Pending);
    CHECK(life->retireOnRenderThread(9) == Result::Done && frees == 1 && life->ack(9));
    CHECK(!claim->open() && life->retireOnRenderThread(9) == Result::Blocked && frees == 1);
  }
  static void concurrent_admission_and_containment() {
    for (int iteration = 0; iteration < 100; ++iteration) {
      RetirementClaim claim(9); CHECK(claim.enterCleanup(9));
      std::atomic<bool> start{false};
      RetirementClaim::Admission result = RetirementClaim::Admission::Existing;
      std::thread admit([&] { while (!start.load()) std::this_thread::yield(); result = claim.admitCleanupStop(9); });
      std::thread contain([&] { while (!start.load()) std::this_thread::yield(); claim.contain(); });
      start = true; admit.join(); contain.join();
      CHECK(claim.contained() && !claim.eligible());
      if (result == RetirementClaim::Admission::New) {
        CHECK(claim.recordStopSubmitted(9) && claim.recordStopCompleted(9));
      } else CHECK(result == RetirementClaim::Admission::Rejected);
      CHECK(claim.admitCleanupStop(9) == RetirementClaim::Admission::Rejected && !claim.claim(9));
    }
  }
public:
  static void run() {
    stop_settlement_orders(); actual_access_transition(); recognize_revoked_cleanup();
    claim_publication_crosses_containment();
    render_order(true); render_order(false);
    renderer_first_then_internal_failure();
    concurrent_admission_and_containment();
  }
};
int main(int argc, char** argv) { QCoreApplication app(argc, argv); TestRetirementClaim::run(); }
