#include "lifetime.h"
#include "lifetime_qt.h"
#include "module_channel.h"
#include <utility>

namespace HealthyRetirement {
bool Slot::bindRendererOwner(const std::shared_ptr<QtToken>& token, const void* item)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || !token || token->slot().get() != this || !item || rendererItem_) return false;
  rendererToken_ = token;
  rendererItem_ = item;
  return true;
}

std::shared_ptr<RendererIncarnation> Slot::bindRenderer(const std::shared_ptr<QtToken>& token,
                                                       const void* item)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (closed_ || !token || rendererToken_.lock() != token || !item || item != rendererItem_ ||
      rendererIncarnation_) return {};
  rendererIncarnation_ = std::shared_ptr<RendererIncarnation>(new RendererIncarnation);
  return rendererIncarnation_;
}

bool Slot::rejectRendererFailure(const std::shared_ptr<QtToken>& token,
                                 const std::shared_ptr<RendererIncarnation>& incarnation,
                                 const std::shared_ptr<Lifetime>& lifetime, Identity identity)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!rendererMatchesLocked(token, incarnation, lifetime, identity)) return false;
  std::lock_guard<std::mutex> lifetimeLock(lifetime->mutex_);
  if (!lifetime->ownerValid_ || lifetime->facts_.phase != Phase::Active || !lifetime->facts_.renders) return false;
  closed_ = true;
  lifetime->ownerValid_ = false;
  lifetime->retain();
  return true;
}

bool Slot::rendererMatches(const std::shared_ptr<QtToken>& token,
                           const std::shared_ptr<RendererIncarnation>& incarnation,
                           const std::shared_ptr<Lifetime>& lifetime, Identity identity) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return rendererMatchesLocked(token, incarnation, lifetime, identity);
}

bool Slot::rendererMatchesLocked(const std::shared_ptr<QtToken>& token,
                                 const std::shared_ptr<RendererIncarnation>& incarnation,
                                 const std::shared_ptr<Lifetime>& lifetime, Identity identity) const
{
  if (closed_ || !token || rendererToken_.lock() != token || !incarnation ||
      incarnation != rendererIncarnation_ || !lifetime || lifetime != lifetime_) return false;
  const auto claim = claimState_ ? std::atomic_load(&claimState_->claim) : nullptr;
  if (claim && !claim->open()) return false;
  std::lock_guard<std::mutex> lifetimeLock(lifetime->mutex_);
  return lifetime->generation_ == generation_ && lifetime->ownerValid_ && lifetime->facts_.phase == Phase::Active &&
      !lifetime->facts_.admissionClosed && lifetime->facts_.renders && lifetime->matches(identity);
}
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
