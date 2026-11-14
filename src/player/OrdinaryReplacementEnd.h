#pragma once
#include <cstdint>
#include "PlaybackIntent.h"

// One bounded, user-requested replacement; no recovery identity is inferred.
class OrdinaryReplacementEnd {
public:
  struct Identity { std::uint64_t epoch = 0, request = 0, binding = 0, revision = 0; };
  bool arm(std::int64_t oldEntry, std::uint64_t beforeStart, Identity identity, std::int64_t deadlineMs, bool stopAdmitted);
  bool preserve(PlaybackIntent::ChangeKind kind, Identity identity, std::int64_t nowMs);
  void started(std::uint64_t currentStart);
  void expire(std::int64_t nowMs);
  bool consume(std::int64_t entry, bool stop, std::uint64_t currentStart, Identity identity, std::int64_t nowMs);
  void revoke();
private:
  std::int64_t entry_ = 0, deadline_ = 0;
  std::uint64_t beforeStart_ = 0;
  Identity identity_;
};
