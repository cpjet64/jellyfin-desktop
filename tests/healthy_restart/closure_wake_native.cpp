#include "closure_wake.h"
#include "module_channel.h"

namespace HealthyRetirement {
bool WakeClosure::unregisterOnce(const std::function<void()>& unregister,
                                 const HealthyModuleIdentity::VerifiedModule& module) noexcept
{
  closeAdmission();
  if (!generation_ || !module.stillValid(generation_) ||
      module.wakeSourceContract(generation_) != HealthyModuleIdentity::SourceContract::BuilderSourceQualified ||
      !unregister || attempted_.exchange(true)) return false;
  try { unregister(); } catch (...) { failed_.store(true); return false; }
  if (!module.stillValid(generation_)) { failed_.store(true); return false; }
  unregistered_.store(true);
  return true;
}
}
