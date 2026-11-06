#define HEALTHY_RETIREMENT_TESTING
#include "healthy_restart/retirement_claim.h"
#include <atomic>
#include <cstdlib>
#include <thread>

using HealthyRetirement::RetirementClaim;
class TestRetirementClaim {
  using Claim = RetirementClaim;
  static void check(bool value) { if (!value) std::abort(); }
  static std::shared_ptr<Claim> ready()
  {
    auto claim = Claim::makeArmReservation(7);
    check(claim && claim->reserveArm(7) && claim->bindArm(7, {1, 2, 3, 4}));
    check(claim->qualifyRetirement(7, {1, 2, 3, 4}));
    return claim;
  }
  static bool admit(const std::shared_ptr<Claim>& claim, Claim::Dispatch operation)
  { return claim->admitRestoreDispatch(7, {1, 2, 3, 4}, operation); }
  static void identitiesAndOneShot()
  {
    auto claim = ready();
    check(!admit(claim, Claim::Dispatch::Seek));
    check(!claim->admitRestoreDispatch(8, {1, 2, 3, 4}, Claim::Dispatch::Load));
    check(!claim->admitRestoreDispatch(7, {1, 2, 3, 5}, Claim::Dispatch::Load));
    check(!claim->admitRestoreDispatch(7, {1, 2, 3, 4}, static_cast<Claim::Dispatch>(4)));
    check(admit(claim, Claim::Dispatch::Load));
    check(!admit(claim, Claim::Dispatch::Load));
    check(admit(claim, Claim::Dispatch::Seek));
    check(!admit(claim, Claim::Dispatch::Seek));
  }
  static void revocationAndCleanup()
  {
    auto revoked = ready(); revoked->revoke(); check(!admit(revoked, Claim::Dispatch::Load));
    auto contained = ready(); contained->contain(); check(!admit(contained, Claim::Dispatch::Load));
    auto cleanup = ready(); check(cleanup->enterCleanup(7)); check(!admit(cleanup, Claim::Dispatch::Load));
    auto admitted = ready(); check(admit(admitted, Claim::Dispatch::Load));
    admitted->revoke(); check(!admit(admitted, Claim::Dispatch::Seek));
  }
  static void concurrentAdmission()
  {
    for (int i = 0; i < 1000; ++i) {
      auto claim = ready();
      std::atomic<bool> start{false};
      bool first = false, second = false;
      std::thread a([&] { while (!start.load()) {} first = admit(claim, Claim::Dispatch::Load); });
      std::thread b([&] { while (!start.load()) {} second = admit(claim, Claim::Dispatch::Load); });
      start.store(true); a.join(); b.join();
      check(first != second);
    }
  }
  static void concurrentRevocation()
  {
    for (int i = 0; i < 1000; ++i) {
      auto claim = ready();
      std::atomic<bool> start{false};
      bool admitted = false;
      std::thread a([&] { while (!start.load()) {} admitted = admit(claim, Claim::Dispatch::Load); });
      std::thread b([&] { while (!start.load()) {} claim->contain(); });
      start.store(true); a.join(); b.join();
      check(claim->contained() && !claim->open());
      check(!admit(claim, Claim::Dispatch::Load) && !admit(claim, Claim::Dispatch::Seek));
      if (admitted) check(claim->state_.load() & Claim::LoadAdmitted);
    }
  }
public:
  static void run() { identitiesAndOneShot(); revocationAndCleanup(); concurrentAdmission(); concurrentRevocation(); }
};
int main() { TestRetirementClaim::run(); }
