#include "closure_wake.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using HealthyRetirement::WakeClosure;
static void require(bool value) { if (!value) std::abort(); }

static void rejected_native_barrier()
{
  int deliveries = 0, unregisters = 0;
  WakeClosure wake(3, [&](std::uint64_t) { ++deliveries; });
  require(!wake.unregisterOnce([&] { ++unregisters; }, WakeClosure::Barrier::Unqualified));
  WakeClosure::callback(&wake);
  require(!wake.drained() && unregisters == 0 && deliveries == 0);
}

static void inflight_and_late_callback()
{
  std::atomic<bool> entered{false}, release{false};
  std::atomic<int> deliveries{0};
  WakeClosure wake(3, [&](std::uint64_t generation) {
    require(generation == 3); ++deliveries; entered = true;
    while (!release.load()) std::this_thread::yield();
  });
  require(wake.deliveryAllowed(3) && !wake.deliveryAllowed(2));
  std::thread callback([&] { WakeClosure::callback(&wake); });
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (!entered.load() && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
  require(entered.load());
  int unregisters = 0;
  require(wake.unregisterOnce([&] { ++unregisters; }, WakeClosure::Barrier::TestSingleSlot));
  require(!wake.drained() && !wake.deliveryAllowed(3));
  release = true; callback.join();
  require(wake.drained());
  WakeClosure::callback(&wake);
  require(deliveries == 1 && unregisters == 1 && wake.drained() && !wake.deliveryAllowed(3));
  require(!wake.unregisterOnce([&] { ++unregisters; }, WakeClosure::Barrier::TestSingleSlot));
}

int main()
{
  rejected_native_barrier(); inflight_and_late_callback();
}
