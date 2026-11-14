#include "RecoveryIdentity.h"
#include <limits>
#include <mutex>

std::uint64_t RecoveryIdentity::Counter::take()
{
  if (!next_ || next_ > maximum_) return 0;
  const auto value = next_;
  next_ = value == maximum_ ? 0 : value + 1;
  return value;
}
std::uint64_t RecoveryIdentity::take(Domain domain)
{
  constexpr auto max53 = (std::uint64_t{1} << 53) - 1;
  constexpr auto max49 = (std::uint64_t{1} << 49) - 1;
  constexpr auto max59 = (std::uint64_t{1} << 59) - 1;
  constexpr auto max60 = (std::uint64_t{1} << 60) - 1;
  constexpr auto max61 = (std::uint64_t{1} << 61) - 1;
  constexpr auto max64 = std::numeric_limits<std::uint64_t>::max();
  static std::mutex mutex;
  static Counter counters[] = {{1, max53}, {1, max49}, {1, max59}, {1, max60}, {1, max60}, {1, max61},
                               {1, max64}, {1, max64}, {1, max53}, {1, max53}, {1, max53}};
  const auto index = static_cast<unsigned>(domain);
  if (index >= static_cast<unsigned>(Domain::Count)) return 0;
  std::lock_guard<std::mutex> lock(mutex);
  return counters[index].take();
}
