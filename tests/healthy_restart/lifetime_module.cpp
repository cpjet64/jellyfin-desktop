#include "lifetime.h"
#include "module_channel.h"
#include <utility>

namespace HealthyRetirement {
namespace {
struct ModuleGuard {
  std::function<bool()> capability;
  std::shared_ptr<const std::atomic_bool> revoked;
  std::shared_ptr<const std::atomic_bool> stop;
};
}
#ifndef HEALTHY_RETIREMENT_TESTING
bool Slot::bindModule(std::shared_ptr<const HealthyModuleIdentity::VerifiedModule> module,
                      std::shared_ptr<const std::atomic_bool> revoked)
{
  if (!module) return false;
  const auto generation = generation_;
  return bindGuard([module = std::move(module), generation] {
    return module->stillValid(generation) && module->renderSourceContract(generation) ==
        HealthyModuleIdentity::SourceContract::BuilderSourceQualified;
  }, std::move(revoked));
}
#endif
bool Slot::bindGuard(std::function<bool()> capability, std::shared_ptr<const std::atomic_bool> revoked)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || creating_ || lifetime_ || moduleQualification_ || !capability || !revoked) return false;
  auto guard = std::make_shared<ModuleGuard>(ModuleGuard{std::move(capability), std::move(revoked), {}});
  auto valid = [guard] {
    const auto stop = std::atomic_load(&guard->stop);
    return !guard->revoked->load() && (!stop || stop->load()) && guard->capability();
  };
  try { if (!valid()) return false; } catch (...) { return false; }
  moduleQualification_ = std::move(valid);
  retirementPermit_ = [guard] {
    const auto stop = std::atomic_load(&guard->stop);
    return stop && stop->load();
  };
  bindStop_ = [guard](std::shared_ptr<const std::atomic_bool> lease) {
    if (!lease || !lease->load() || std::atomic_load(&guard->stop)) return false;
    std::atomic_store(&guard->stop, std::move(lease));
    return true;
  };
  return true;
}
#ifdef HEALTHY_RETIREMENT_TESTING
bool Slot::bindModuleForTest(std::function<bool()> capability, std::shared_ptr<const std::atomic_bool> revoked)
{
  return bindGuard(std::move(capability), std::move(revoked));
}
bool Slot::bindStopLeaseForTest(std::shared_ptr<const std::atomic_bool> lease)
{
  return bindStopLease(std::move(lease));
}
#endif
bool Slot::bindStopLease(std::shared_ptr<const std::atomic_bool> lease)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return !closed_ && lifetime_ && bindStop_ && bindStop_(std::move(lease));
}
}
