#pragma once
#include <atomic>
#include <cstdint>
#include <memory>

class PlayerComponent;
class TestRetirementClaim;
namespace HealthyRetirement {
class Lifetime;
class Slot;
class FixtureGraph;
class GraphAccessOps;
// One atomic record orders playback revocation, cleanup admission and render free.
class RetirementClaim final {
public:
  enum class Admission { New, Existing, Rejected };
  enum class Dispatch { Load, Seek };
  struct ArmIdentity {
    std::uint64_t host = 0, core = 0, revision = 0, attempt = 0;
    bool operator==(const ArmIdentity& other) const noexcept
    { return host == other.host && core == other.core && revision == other.revision && attempt == other.attempt; }
  };
  explicit RetirementClaim(std::uint64_t generation) : generation_(generation), state_(RetirementEligible) {}
  std::uint64_t generation() const noexcept { return generation_; }
  bool eligible() const noexcept
  {
    const auto state = state_.load();
    return generation_ && (state & RetirementEligible) && !(state & (Revoked | RetirementDenied));
  }
  bool committed() const noexcept
  {
    const auto state = state_.load();
    return generation_ && (state & RetirementEligible) && (state & RenderClaimed) &&
           !(state & (Revoked | RetirementDenied));
  }
  bool open() const noexcept
  {
    const auto state = state_.load();
    return generation_ && (state & RetirementEligible) &&
           !(state & (CleanupOnly | RenderClaimed | Revoked | RetirementDenied));
  }
  void revoke() noexcept { state_.fetch_or(Revoked); }
  void contain() noexcept { state_.fetch_or(Revoked | Contained); }
  bool contained() const noexcept { return state_.load() & Contained; }
private:
  friend class Lifetime;
  friend class Slot;
  friend class FixtureGraph;
  friend class GraphAccessOps;
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestRetirementClaim;
#endif
  enum Bits : std::uint32_t {
    CleanupOnly = 1, StopAdmitted = 2, StopSubmitted = 4, StopCompleted = 8,
    RenderClaimed = 16, RenderRetired = 32, Revoked = 64, Contained = 128, StopQualified = 256,
    Reservation = 512, ArmPending = 1024, Binding = 2048, StopOnlyBound = 4096,
    RetirementEligible = 8192, RetirementDenied = 16384,
    LoadAdmitted = 32768, SeekAdmitted = 65536
  };
  struct ReservationTag {};
  RetirementClaim(std::uint64_t generation, ReservationTag) : generation_(generation), state_(Reservation) {}
  static std::shared_ptr<RetirementClaim> makeArmReservation(std::uint64_t generation)
  { return generation ? std::shared_ptr<RetirementClaim>(new RetirementClaim(generation, ReservationTag{})) : nullptr; }
  bool reserveArm(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto expected = static_cast<std::uint32_t>(Reservation);
    return state_.compare_exchange_strong(expected, expected | ArmPending);
  }
  bool bindArm(std::uint64_t generation, ArmIdentity identity) noexcept
  {
    if (!generation || generation != generation_ || !identity.host || !identity.core ||
        !identity.revision || !identity.attempt) return false;
    auto current = state_.load();
    for (;;) {
      if (!(current & Reservation) || !(current & ArmPending) || (current & (Binding | StopOnlyBound))) return false;
      if (state_.compare_exchange_weak(current, current | Binding)) break;
    }
    armIdentity_ = identity;
    state_.fetch_or(StopOnlyBound);
    state_.fetch_and(~static_cast<std::uint32_t>(Binding));
    return true;
  }
  bool matchesArm(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    const auto state = state_.load();
    return generation && generation == generation_ && (state & StopOnlyBound) && !(state & Binding) &&
           armIdentity_ == identity;
  }
  // Admission shares the revocation/containment atomic; success is one-shot.
  bool admitRestoreDispatch(std::uint64_t generation, ArmIdentity identity, Dispatch operation) const noexcept
  {
    if ((operation != Dispatch::Load && operation != Dispatch::Seek) || !matchesArm(generation, identity)) return false;
    const auto flag = operation == Dispatch::Load ? LoadAdmitted : SeekAdmitted;
    auto current = state_.load();
    for (;;) {
      if (!(current & RetirementEligible) ||
          (current & (CleanupOnly | RenderClaimed | Revoked | RetirementDenied | flag)) ||
          (operation == Dispatch::Seek && !(current & LoadAdmitted))) return false;
      if (state_.compare_exchange_weak(current, current | flag)) return true;
    }
  }
  bool installable(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    const auto state = state_.load();
    return (state & Reservation) && !(state & (CleanupOnly | Revoked | RetirementDenied | RetirementEligible)) &&
           matchesArm(generation, identity);
  }
  bool qualifyRetirement(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    if (!matchesArm(generation, identity)) return false;
    auto current = state_.load();
    for (;;) {
      if (!(current & Reservation) || (current & (CleanupOnly | Revoked | RetirementDenied | RetirementEligible)))
        return false;
      if (state_.compare_exchange_weak(current, current | RetirementEligible)) return true;
    }
  }
  bool denyRetirement(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    if (!matchesArm(generation, identity)) return false;
    auto current = state_.load();
    for (;;) {
      if (!(current & Reservation) || (current & RetirementEligible)) return false;
      if (state_.compare_exchange_weak(current, current | CleanupOnly | RetirementDenied)) return true;
    }
  }
  bool enterCleanup(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto current = state_.load();
    for (;;) {
      if (current & Revoked) return false;
      if (current & CleanupOnly) return true;
      const auto denied = (current & Reservation) && !(current & RetirementEligible) ? RetirementDenied : 0;
      if (state_.compare_exchange_weak(current, current | CleanupOnly | denied)) return true;
    }
  }
  // Recognition permits joining an existing ledger; it grants no new admission.
  bool recognizeCleanup(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    const auto state = state_.load();
    return (state & CleanupOnly) && !(state & Contained);
  }
  Admission admitCleanupStop(std::uint64_t generation) const noexcept
  {
    if (state_.load() & Reservation) return Admission::Rejected;
    return admitStop(generation);
  }
  Admission admitCleanupStop(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    if (!matchesArm(generation, identity)) return Admission::Rejected;
    return admitStop(generation);
  }
  Admission admitStop(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return Admission::Rejected;
    auto current = state_.load();
    for (;;) {
      if ((current & Revoked) || !(current & CleanupOnly)) return Admission::Rejected;
      if (current & StopAdmitted) return Admission::Existing;
      if (current & RenderClaimed) return Admission::Rejected;
      if (state_.compare_exchange_weak(current, current | StopAdmitted)) return Admission::New;
    }
  }
  bool recordStopSubmitted(std::uint64_t generation) const noexcept
  { return record(generation, StopAdmitted, StopSubmitted); }
  bool recordStopCompleted(std::uint64_t generation) const noexcept
  { return record(generation, StopAdmitted, StopCompleted); }
  bool qualifyCleanup(std::uint64_t generation) const noexcept
  {
    if (state_.load() & Reservation) return false;
    return qualifyStop(generation);
  }
  bool qualifyCleanup(std::uint64_t generation, ArmIdentity identity) const noexcept
  {
    if (!matchesArm(generation, identity)) return false;
    return qualifyStop(generation);
  }
  bool qualifyStop(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto current = state_.load();
    for (;;) {
      if ((current & Revoked) || !(current & CleanupOnly)) return false;
      if (current & StopQualified) return true;
      if ((current & StopAdmitted) &&
          ((current & (StopSubmitted | StopCompleted)) != (StopSubmitted | StopCompleted))) return false;
      if (state_.compare_exchange_weak(current, current | StopQualified)) return true;
    }
  }
  bool claim(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto current = state_.load();
    for (;;) {
      if (!(current & RetirementEligible) || (current & (Revoked | RenderClaimed | RetirementDenied))) return false;
      if ((current & CleanupOnly) && !(current & StopQualified)) return false;
      if ((current & StopAdmitted) &&
          ((current & (StopSubmitted | StopCompleted)) != (StopSubmitted | StopCompleted))) return false;
      if (state_.compare_exchange_weak(current, current | RenderClaimed)) return true;
    }
  }
  bool recordRenderRetired(std::uint64_t generation) const noexcept
  { return record(generation, RenderClaimed, RenderRetired); }
  bool record(std::uint64_t generation, std::uint32_t required, std::uint32_t flag) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto current = state_.load();
    for (;;) {
      if (!(current & required) || (current & flag)) return false;
      if (state_.compare_exchange_weak(current, current | flag)) return true;
    }
  }
  const std::uint64_t generation_;
  ArmIdentity armIdentity_{};
  mutable std::atomic<std::uint32_t> state_;
};
}
