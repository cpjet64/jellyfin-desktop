#include "lifetime.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <thread>

using namespace HealthyRetirement;
static void check(bool value) { if (!value) std::abort(); }

static void invalidated_during_free(int mode)
{
  auto revoked = std::make_shared<std::atomic_bool>(false);
  auto lease = std::make_shared<std::atomic_bool>(true);
  std::atomic<bool> capability{true}, entered{false}, release{false};
  std::atomic<int> frees{0};
  auto slot = std::make_shared<Slot>(9, 100, [] { return std::int64_t{10}; });
  check(slot->bindModuleForTest([&] { return capability.load(); }, revoked));
  check(slot->beginCreation() && slot->beforeCreate());
  int context = 0;
  check(slot->recordCreated(&context));
  Operations ops;
  ops.currentIdentity = [] { return Identity{1, 2, 3}; };
  ops.unregisterCallback = [](void*) {};
  ops.invalidateQueued = [](std::uint64_t) { return true; };
  ops.freeContext = [&](void*) { ++frees; entered = true; while (!release.load()) std::this_thread::yield(); };
  ops.clearPointer = [](void*) { return true; };
  auto life = slot->install(Identity{1, 2, 3}, &context, ops);
  check(life && slot->bindStopLeaseForTest(lease));
  check(slot->request(9) == Result::Pending);
  if (mode == 0) lease->store(false);
  std::thread worker([&] { check(life->retireOnRenderThread(9) == Result::Blocked); });
  if (mode != 0) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!entered.load() && std::chrono::steady_clock::now() < limit) std::this_thread::yield();
    check(entered.load());
    if (mode == 1) lease->store(false);
    if (mode == 2) revoked->store(true);
    if (mode == 3) capability = false;
  }
  release = true; worker.join();
  check(!life->ack(9));
  check(frees == (mode == 0 ? 0 : 1));
}

int main()
{
  invalidated_during_free(0); invalidated_during_free(1);
  invalidated_during_free(2); invalidated_during_free(3);
}
