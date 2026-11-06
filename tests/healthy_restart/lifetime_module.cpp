#include "lifetime.h"
#include "module_channel.h"
#include <utility>

namespace HealthyRetirement {
namespace {
struct ModuleGuard {
  std::function<bool()> capability;
  std::shared_ptr<const std::atomic_bool> revoked;
  std::shared_ptr<ClaimState> claims;
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
  auto claims = std::make_shared<ClaimState>();
  auto guard = std::make_shared<ModuleGuard>(ModuleGuard{std::move(capability), std::move(revoked), claims});
  auto valid = [guard] {
    const auto claim = std::atomic_load(&guard->claims->claim);
    return !guard->revoked->load() && (!claim || claim->eligible()) && guard->capability();
  };
  try { if (!valid()) return false; } catch (...) { return false; }
  moduleQualification_ = std::move(valid);
  claimState_ = std::move(claims);
  return true;
}
#ifdef HEALTHY_RETIREMENT_TESTING
bool Slot::bindModuleForTest(std::function<bool()> capability, std::shared_ptr<const std::atomic_bool> revoked)
{
  return bindGuard(std::move(capability), std::move(revoked));
}
bool Slot::bindStopClaimForTest(std::shared_ptr<const RetirementClaim> claim)
{
  return bindStopClaim(std::move(claim));
}
#endif
bool Slot::bindStopClaim(std::shared_ptr<const RetirementClaim> claim)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || !claimState_ || !claim || claim->generation() != generation_ || !claim->open()) return false;
  const auto bound = std::atomic_load(&claimState_->claim);
  if (bound) return bound == claim;
  std::atomic_store(&claimState_->claim, std::move(claim));
  return true;
}
}
