#pragma once
#include <cstdint>

namespace RecoveryIdentity {
enum class Domain { Load, Observation, PlaylistQuery, Pause, Unpause, SeekReply, SeekOperation,
                    Usability, FixtureLoad, FixtureObservation, FixtureSeek, Count };
// Injectable bounded allocator for deterministic exhaustion tests.
class Counter {
public:
  Counter(std::uint64_t next, std::uint64_t maximum) : next_(next), maximum_(maximum) {}
  std::uint64_t take();
private:
  std::uint64_t next_, maximum_;
};
// Process-wide, checked and never reset when a Player or Core binding changes.
std::uint64_t take(Domain domain);
}
