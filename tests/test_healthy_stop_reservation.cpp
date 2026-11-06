#include "graph_access.h"
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace HealthyRetirement;
#define CHECK(v) do { if (!(v)) { std::fprintf(stderr, "check failed at %d\n", __LINE__); std::abort(); } } while (false)

class TestRetirementClaim {
  using Key = RetirementClaim::ArmIdentity;
  static constexpr Key key{1, 2, 3, 4};
  static void arm_and_wrong_identity() {
    auto c = RetirementClaim::makeArmReservation(9);
    CHECK(c && !c->open() && !c->eligible() && c->reserveArm(9));
    CHECK(!c->reserveArm(9) && !c->claim(9));
    CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::Rejected);
    CHECK(!c->bindArm(10, key));
    auto incomplete = key; incomplete.core = 0;
    CHECK(!c->bindArm(9, incomplete));
    CHECK(c->bindArm(9, key) && !c->bindArm(9, key));
    auto wrong = key; ++wrong.attempt;
    CHECK(!c->matchesArm(9, wrong) && !c->qualifyRetirement(9, wrong));
    CHECK(c->installable(9, key) && c->qualifyRetirement(9, key));
    CHECK(!c->qualifyRetirement(9, key) && c->open() && c->eligible());
  }
  static void failed_install_is_permanent() {
    auto c = RetirementClaim::makeArmReservation(9);
    CHECK(c->reserveArm(9) && c->bindArm(9, key));
    auto wrong = key; ++wrong.core;
    CHECK(!c->denyRetirement(9, wrong) && c->installable(9, key));
    CHECK(c->denyRetirement(9, key));
    CHECK(!c->open() && !c->eligible() && !c->installable(9, key));
    CHECK(!c->qualifyRetirement(9, key) && !c->claim(9));
    CHECK(c->admitCleanupStop(9, wrong) == RetirementClaim::Admission::Rejected);
    CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::New);
    CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::Existing);
    c->contain();
    CHECK(c->recordStopCompleted(9) && c->recordStopSubmitted(9));
    CHECK(!c->qualifyRetirement(9, key) && !c->claim(9) && !c->eligible());
    auto live = RetirementClaim::makeArmReservation(9);
    CHECK(live->reserveArm(9) && live->bindArm(9, key) && live->denyRetirement(9, key));
    CHECK(live->admitCleanupStop(9, key) == RetirementClaim::Admission::New);
    CHECK(live->recordStopSubmitted(9) && live->recordStopCompleted(9));
    CHECK(!live->qualifyCleanup(9, wrong) && live->qualifyCleanup(9, key));
    CHECK(!live->qualifyRetirement(9, key) && !live->claim(9));
  }
  static void containment_during_arm() {
    auto c = RetirementClaim::makeArmReservation(9);
    CHECK(c->reserveArm(9)); c->contain();
    CHECK(c->bindArm(9, key) && c->matchesArm(9, key));
    CHECK(!c->qualifyRetirement(9, key) && !c->claim(9));
    CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::Rejected);
    CHECK(c->contained() && !c->eligible());
  }
  static void pending_cannot_bind_renderer() {
    auto c = RetirementClaim::makeArmReservation(9);
    CHECK(c->reserveArm(9));
    int context = 0, frees = 0;
    Operations ops;
    ops.now = [] { return std::int64_t{10}; };
    ops.currentIdentity = [] { return Identity{1, 2, 3}; };
    ops.unregisterCallback = [](void*) {};
    ops.barrier = [] { return BarrierResult::Ready; };
    ops.invalidateQueued = [](std::uint64_t) { return true; };
    ops.freeContext = [&](void*) { ++frees; };
    ops.clearPointer = [](void*) { return true; };
    Lifetime life(9, Identity{1, 2, 3}, &context, 100, ops, Barrier::TestQualified);
    CHECK(!life.bindClaimForTest(c) && !c->claim(9) && frees == 0);
  }
  static void actual_graph_access() {
    auto access = std::make_shared<GraphAccess>();
    access->guiThread = std::this_thread::get_id(); access->guiQtThread = QThread::currentThread();
    auto* graph = reinterpret_cast<FixtureGraph*>(static_cast<std::uintptr_t>(1)); access->graph.store(graph);
    auto c = RetirementClaim::makeArmReservation(9);
    CHECK(c->reserveArm(9)); std::atomic_store(&access->intentClaim, c);
    CHECK(GraphAccessOps::beginCleanupForTest(access, 9) == graph);
    CHECK(!GraphAccessOps::graphOnGuiForTest(access) && !c->eligible());
    CHECK(c->bindArm(9, key));
    CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::New);
    CHECK(!c->qualifyRetirement(9, key));
  }
  static void racing_qualification_and_cleanup() {
    for (int i = 0; i < 100; ++i) {
      auto c = RetirementClaim::makeArmReservation(9);
      CHECK(c->reserveArm(9) && c->bindArm(9, key));
      std::atomic<bool> start{false}; bool qualified = false;
      std::thread install([&] { while (!start.load()) std::this_thread::yield(); qualified = c->qualifyRetirement(9, key); });
      std::thread cleanup([&] { while (!start.load()) std::this_thread::yield(); CHECK(c->enterCleanup(9)); });
      start = true; install.join(); cleanup.join();
      CHECK(!c->open());
      CHECK(c->eligible() == qualified);
      CHECK(!c->qualifyRetirement(9, key));
      c->contain(); CHECK(!c->eligible() && !c->claim(9));
    }
  }
  static void racing_qualification_and_containment() {
    for (int i = 0; i < 100; ++i) {
      auto c = RetirementClaim::makeArmReservation(9);
      CHECK(c->reserveArm(9) && c->bindArm(9, key));
      std::atomic<bool> start{false};
      std::thread install([&] { while (!start.load()) std::this_thread::yield(); (void)c->qualifyRetirement(9, key); });
      std::thread contain([&] { while (!start.load()) std::this_thread::yield(); c->contain(); });
      start = true; install.join(); contain.join();
      CHECK(c->contained() && !c->eligible() && !c->open());
      CHECK(!c->qualifyRetirement(9, key) && !c->claim(9));
      CHECK(c->admitCleanupStop(9, key) == RetirementClaim::Admission::Rejected);
    }
  }
public:
  static void run() { arm_and_wrong_identity(); failed_install_is_permanent(); containment_during_arm(); pending_cannot_bind_renderer();
                      actual_graph_access(); racing_qualification_and_cleanup(); racing_qualification_and_containment(); }
};
constexpr TestRetirementClaim::Key TestRetirementClaim::key;
int main(int argc, char** argv) { QCoreApplication app(argc, argv); TestRetirementClaim::run(); }
