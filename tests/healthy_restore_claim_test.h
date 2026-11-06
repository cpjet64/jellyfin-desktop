#pragma once
#ifndef HEALTHY_RETIREMENT_TESTING
#error "The claim test helper requires a consistent test-only claim overlay"
#endif
#include "retirement_claim.h"

class TestRetirementClaim
{
public:
  static std::shared_ptr<HealthyRetirement::RetirementClaim> reserve(std::uint64_t generation)
  {
    auto claim = HealthyRetirement::RetirementClaim::makeArmReservation(generation);
    return claim && claim->reserveArm(generation) ? claim : nullptr;
  }
  static bool bind(const std::shared_ptr<HealthyRetirement::RetirementClaim>& claim,
                   std::uint64_t generation, HealthyRetirement::RetirementClaim::ArmIdentity identity)
  { return claim && claim->bindArm(generation, identity); }
  static bool qualify(const std::shared_ptr<HealthyRetirement::RetirementClaim>& claim,
                      std::uint64_t generation, HealthyRetirement::RetirementClaim::ArmIdentity identity)
  { return claim && claim->qualifyRetirement(generation, identity); }
};
