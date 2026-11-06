#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
namespace HealthyModuleIdentity { class VerifiedModule; }

namespace HealthyRetirement {
// Retained by the graph through native unregister and all admitted callbacks.
// A zero count is meaningful only after a separately qualified unregister.
class WakeClosure {
public:
  enum class Barrier { Unqualified
#ifdef HEALTHY_RETIREMENT_TESTING
    , TestSingleSlot
#endif
  };
  using Delivery = std::function<void(std::uint64_t)>;
  WakeClosure(std::uint64_t generation, Delivery delivery);
  static void callback(void* context) noexcept;
  void closeAdmission() noexcept;
  bool unregisterOnce(const std::function<void()>& unregister,
                      Barrier barrier) noexcept;
  bool unregisterOnce(const std::function<void()>& unregister,
                      const HealthyModuleIdentity::VerifiedModule& module) noexcept;
  bool drained() const noexcept;
  bool deliveryAllowed(std::uint64_t generation) const noexcept;
  std::uint64_t generation() const noexcept { return generation_; }
private:
  const std::uint64_t generation_;
  const Delivery delivery_;
  std::atomic<bool> open_{true}, attempted_{false}, unregistered_{false}, failed_{false};
  std::atomic<unsigned> active_{0};
};
struct WakeUnregisterTask {
  std::shared_ptr<WakeClosure> wake;
  const HealthyModuleIdentity::VerifiedModule* module = nullptr;
  std::atomic_bool returned{false}, succeeded{false};
};
}
