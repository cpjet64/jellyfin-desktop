#include "graph_access.h"
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace HealthyRetirement;
static void check(bool value) { if (!value) std::abort(); }

static void off_thread_invalidation()
{
  auto access = std::make_shared<GraphAccess>();
  access->guiThread = std::this_thread::get_id();
  access->guiQtThread = QThread::currentThread();
  auto claim = std::make_shared<RetirementClaim>(9);
  std::atomic_store(&access->intentClaim, claim);
  auto* poison = reinterpret_cast<FixtureGraph*>(static_cast<std::uintptr_t>(1));
  access->graph.store(poison);
  std::atomic<int> guiCalls{0};
  access->guiInvalidate = [&](FixtureGraph*) { ++guiCalls; };
  std::atomic<bool> start{false};
  std::thread worker([&] {
    while (!start.load()) std::this_thread::yield();
    check(!GraphAccessOps::graphOnGuiForTest(access));
    GraphAccessOps::invalidateForTest(access);
  });
  start = true;
  access->graph.store(nullptr); // GUI destruction races the non-GUI invalidation.
  worker.join();
  check(access->revoked.load() && !claim->eligible() && guiCalls == 0);
}

static void successful_access_does_not_cycle()
{
  auto access = std::make_shared<GraphAccess>();
  auto module = std::make_shared<int>(1);
  std::weak_ptr<GraphAccess> weakAccess = access;
  std::weak_ptr<int> weakModule = module;
  access->capability = GraphAccessOps::capabilityForTest(access,
      [module](std::uint64_t generation) { return *module == 1 && generation == 9; });
  access->closed.emplace(ClosedAck{9, 2, 9, 5, 6, 10});
  check(access->capability(9));
  module.reset(); access.reset();
  check(weakAccess.expired() && weakModule.expired());
}

int main(int argc, char** argv)
{
  QCoreApplication application(argc, argv);
  off_thread_invalidation(); successful_access_does_not_cycle();
}
