#pragma once
#include <atomic>
#include <cstdint>

namespace HealthyRetirement {
class Lifetime;
// One Player-owned object linearizes intent revocation against render free.
class RetirementClaim final {
public:
  explicit RetirementClaim(std::uint64_t generation) : generation_(generation) {}
  std::uint64_t generation() const noexcept { return generation_; }
  bool eligible() const noexcept
  {
    const auto state = state_.load();
    return generation_ && (state == State::Open || state == State::Claimed);
  }
  bool committed() const noexcept { return state_.load() == State::Claimed; }
  bool open() const noexcept { return generation_ && state_.load() == State::Open; }
  void revoke() noexcept
  {
    auto current = state_.load();
    while (current == State::Open || current == State::Claimed) {
      const auto next = current == State::Open ? State::RevokedBefore : State::RevokedAfter;
      if (state_.compare_exchange_weak(current, next)) return;
    }
  }
private:
  friend class Lifetime;
  enum class State { Open, Claimed, RevokedBefore, RevokedAfter };
  bool claim(std::uint64_t generation) const noexcept
  {
    if (!generation || generation != generation_) return false;
    auto expected = State::Open;
    return state_.compare_exchange_strong(expected, State::Claimed);
  }
  const std::uint64_t generation_;
  mutable std::atomic<State> state_{State::Open};
};
}
