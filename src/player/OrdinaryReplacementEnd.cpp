#include "OrdinaryReplacementEnd.h"
#include <limits>

bool OrdinaryReplacementEnd::arm(std::int64_t entry, std::uint64_t start, std::uint64_t revision,
                                 std::uint64_t binding, std::int64_t deadline)
{
  revoke();
  if (entry <= 0 || start == std::numeric_limits<std::uint64_t>::max() || !revision || !binding || deadline <= 0) return false;
  entry_ = entry; beforeStart_ = start; revision_ = revision; binding_ = binding; deadline_ = deadline; return true;
}
bool OrdinaryReplacementEnd::consume(std::int64_t entry, bool stop, std::uint64_t start, std::uint64_t revision,
                                     std::uint64_t binding, std::int64_t now)
{
  if (!entry_ || entry != entry_ || !stop || revision != revision_ || binding != binding_ || now < 0 || now >= deadline_ ||
      start < beforeStart_ || start > beforeStart_ + 1) return false;
  revoke(); return true;
}
void OrdinaryReplacementEnd::revoke() { entry_ = deadline_ = 0; beforeStart_ = revision_ = binding_ = 0; }
