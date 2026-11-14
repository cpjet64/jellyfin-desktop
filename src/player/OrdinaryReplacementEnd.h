#pragma once
#include <cstdint>

// One bounded, user-requested replacement; no recovery identity is inferred.
class OrdinaryReplacementEnd {
public:
  bool arm(std::int64_t oldEntry, std::uint64_t beforeStart, std::uint64_t revision,
           std::uint64_t binding, std::int64_t deadlineMs);
  bool consume(std::int64_t entry, bool stop, std::uint64_t currentStart, std::uint64_t revision,
               std::uint64_t binding, std::int64_t nowMs);
  void revoke();
private:
  std::int64_t entry_ = 0, deadline_ = 0;
  std::uint64_t beforeStart_ = 0, revision_ = 0, binding_ = 0;
};
