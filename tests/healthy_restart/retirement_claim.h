#pragma once
#include <atomic>
#include <cstdint>

class PlayerComponent;
class TestRetirementClaim;
namespace HealthyRetirement {
class Lifetime;
class FixtureGraph;
class GraphAccessOps;
// One atomic record orders playback revocation, cleanup admission and render free.
class RetirementClaim final {
public:
  enum class Admission { New, Existing, Rejected };
  explicit RetirementClaim(std::uint64_t generation) : generation_(generation) {}
  std::uint64_t generation() const noexcept { return generation_; }
  bool eligible() const noexcept { return generation_ && !(state_.load() & Revoked); }
  bool committed() const noexcept
  {
    const auto state = state_.load();
    return generation_ && (state & RenderClaimed) && !(state & Revoked);
  }
  bool open() const noexcept
  {
    return generation_ && !(state_.load() & (CleanupOnly | RenderClaimed | Revoked));
  }
  void revoke() noexcept { state_.fetch_or(Revoked); }
  void contain() noexcept { state_.fetch_or(Revoked | Contained); }
  bool contained() const noexcept { return state_.load() & Contained; }
private:
  friend class Lifetime;
  friend class FixtureGraph;
  friend class GraphAccessOps;
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestRetirementClaim;
#endif
  enum Bits : std::uint32_t {
    CleanupOnly = 1, StopAdmitted = 2, StopSubmitted = 4, StopCompleted = 8,
    RenderClaimed = 16, RenderRetired = 32, Revoked = 64, Contained = 128, StopQualified = 256
  };
  bool enterCleanup(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto current = state_.load();
    for (;;) {
      if (current & Revoked) return false;
      if (current & CleanupOnly) return true;
      if (state_.compare_exchange_weak(current, current | CleanupOnly)) return true;
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
      if (current & (Revoked | RenderClaimed)) return false;
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
  mutable std::atomic<std::uint32_t> state_{0};
};
}
