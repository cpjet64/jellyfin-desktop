#include "capture_capability.h"
#include "lifetime_qt.h"
#include <QCoreApplication>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <thread>

using namespace HealthyRetirement;
#define CHECK(value) do { if (!(value)) { std::fprintf(stderr, "check failed at %d: %s\n", __LINE__, #value); std::abort(); } } while (false)

class TestRendererFailure {
  static constexpr Identity identity{11, 12, 13};
  struct Fixture {
    int owner = 0, foreign = 0, context = 0, frees = 0;
    std::shared_ptr<Slot> slot = std::make_shared<Slot>(9, 1000, [] { return 10; });
    std::shared_ptr<QtToken> token;
    std::shared_ptr<Lifetime> lifetime;
    std::unique_ptr<CaptureController> controller;
    std::shared_ptr<CaptureBridge> bridge;
    std::shared_ptr<RendererIncarnation> incarnation;
    std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
    void setup()
    {
      auto revoked = std::make_shared<std::atomic_bool>(false);
      CHECK(slot->bindModuleForTest([] { return true; }, revoked));
      token = std::make_shared<QtToken>(slot, QCoreApplication::instance(), [] {});
      CHECK(slot->bindRendererOwner(token, &owner));
      CHECK(!slot->bindRendererOwner(token, &foreign));
      incarnation = slot->bindRenderer(token, &owner); CHECK(incarnation);
      CHECK(!slot->bindRenderer(token, &foreign));
      controller = CaptureController::create(slot); CHECK(controller);
      bridge = slot->captureBridge(); CHECK(bridge);
      CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context));
      Operations ops;
      ops.currentIdentity = [] { return identity; };
      ops.unregisterCallback = [](void*) {};
      ops.invalidateQueued = [](std::uint64_t) { return true; };
      ops.freeContext = [this](void*) { ++frees; };
      ops.clearPointer = [](void*) { return true; };
      lifetime = slot->install(identity, &context, std::move(ops)); CHECK(lifetime);
    }
    bool attempt()
    {
      CHECK(lifetime->enterRender(9));
      const bool result = bridge->firstRendererAttempt(token, incarnation, lifetime, identity);
      lifetime->leaveRender();
      return result;
    }
  };
  static void initialFailure()
  {
    Fixture f; f.setup();
    CHECK(f.controller->availability() == CaptureBridge::Status::Pending);
    f.bridge->failFirstRendererForTest_ = true;
    CHECK(!f.attempt());
    CHECK(f.controller->availability() == CaptureBridge::Status::Rejected);
    CHECK(f.lifetime->facts().phase == Phase::Retained && !f.lifetime->facts().renders && !f.frees);
    CHECK(!f.controller->request({1, 2, 3, 4, 5, 0}, 6, CaptureBridge::Phase::Baseline, f.claim));
    CHECK(!f.bridge->issue(identity, 1).request);
    CHECK(!f.bridge->consume({}, identity));
    CHECK(!f.bridge->evidence({}, identity, {}));
    CHECK(!f.controller->request({1, 2, 3, 4, 6, 6}, 6, CaptureBridge::Phase::Target, f.claim));
    CHECK(!f.controller->ready(6, CaptureBridge::Phase::Baseline));
    CHECK(!f.bridge->firstRendererAttempt(f.token, f.incarnation, f.lifetime, identity));
  }
  static void foreignAndStale()
  {
    Fixture current; current.setup(); Fixture stale; stale.setup();
    CHECK(current.lifetime->enterRender(9));
    CHECK(!current.bridge->firstRendererAttempt(current.token, stale.incarnation, current.lifetime, identity));
    CHECK(!current.bridge->firstRendererAttempt(stale.token, current.incarnation, current.lifetime, identity));
    CHECK(!current.bridge->firstRendererAttempt(current.token, current.incarnation, stale.lifetime, identity));
    current.lifetime->leaveRender();
    CHECK(current.controller->availability() == CaptureBridge::Status::Pending);
    CHECK(current.attempt());
    CHECK(current.lifetime->enterRender(9));
    CHECK(!current.bridge->firstRendererAttempt(stale.token, stale.incarnation, current.lifetime, identity));
    current.lifetime->leaveRender();
    CHECK(current.bridge->framebuffer(identity, {7, 160, 90, 0, 0, 0, true, true}));
    CHECK(current.controller->availability() == CaptureBridge::Status::Ready);
  }
  static void invalidatedBeforeAttempt()
  {
    Fixture f; f.setup(); CHECK(f.lifetime->enterRender(9));
    std::thread invalidator([&] { f.slot->invalidate(9); }); invalidator.join();
    f.bridge->failFirstRendererForTest_ = true;
    CHECK(!f.bridge->firstRendererAttempt(f.token, f.incarnation, f.lifetime, identity));
    f.lifetime->leaveRender();
    CHECK(f.controller->availability() == CaptureBridge::Status::Rejected && !f.frees);
  }
  static void reusedIdentityAndWrongGeneration()
  {
    alignas(int) unsigned char storage[sizeof(int)];
    auto* original = new (storage) int(1);
    auto slot = std::make_shared<Slot>(8, 1000, [] { return 10; });
    auto token = std::make_shared<QtToken>(slot, QCoreApplication::instance(), [] {});
    CHECK(slot->bindRendererOwner(token, original));
    auto marker = slot->bindRenderer(token, original); CHECK(marker);
    auto* replacement = new (storage) int(2);
    CHECK(!slot->bindRenderer(token, replacement));
    Fixture f; f.setup(); CHECK(f.lifetime->enterRender(9));
    CHECK(!f.bridge->firstRendererAttempt(f.token, marker, f.lifetime, identity));
    f.lifetime->leaveRender();
    CHECK(f.controller->availability() == CaptureBridge::Status::Pending);
    CHECK(!f.slot->current()->enterRender(10));
  }
  static void invalidationRace()
  {
    Fixture f; f.setup(); CHECK(f.lifetime->enterRender(9));
    f.bridge->failFirstRendererForTest_ = true;
    std::atomic_bool go{false};
    std::thread invalidator([&] { while (!go.load()) std::this_thread::yield(); f.slot->invalidate(9); });
    go = true;
    CHECK(!f.bridge->firstRendererAttempt(f.token, f.incarnation, f.lifetime, identity));
    invalidator.join(); f.lifetime->leaveRender();
    CHECK(f.controller->availability() == CaptureBridge::Status::Rejected && !f.frees);
    CHECK(f.lifetime->facts().phase == Phase::Retained && !f.lifetime->facts().renders);
  }
public:
  static void run()
  {
    initialFailure(); foreignAndStale(); invalidatedBeforeAttempt();
    reusedIdentityAndWrongGeneration(); invalidationRace();
  }
};
constexpr Identity TestRendererFailure::identity;
int main(int argc, char** argv) { QCoreApplication app(argc, argv); TestRendererFailure::run(); }
