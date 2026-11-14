#include "OrdinaryReplacementEnd.h"
#include <limits>

bool OrdinaryReplacementEnd::arm(std::int64_t entry, std::uint64_t start, Identity identity,
                                 std::int64_t deadline, bool stopAdmitted)
{
  revoke();
  if (!stopAdmitted || entry <= 0 || start == std::numeric_limits<std::uint64_t>::max() ||
      !identity.epoch || !identity.request || !identity.binding || !identity.revision || deadline <= 0) return false;
  entry_ = entry; beforeStart_ = start; identity_ = identity; deadline_ = deadline; return true;
}
bool OrdinaryReplacementEnd::preserve(PlaybackIntent::ChangeKind kind, Identity identity, std::int64_t now)
{
  const bool allowed = kind == PlaybackIntent::ChangeKind::Pause || kind == PlaybackIntent::ChangeKind::Resume ||
      kind == PlaybackIntent::ChangeKind::Audio || kind == PlaybackIntent::ChangeKind::Subtitle;
  if (!allowed || !entry_ || identity.epoch != identity_.epoch || identity.request != identity_.request ||
      identity.binding != identity_.binding || identity.revision <= identity_.revision || now < 0 || now >= deadline_) {
    revoke(); return false;
  }
  identity_.revision = identity.revision;
  return true;
}
void OrdinaryReplacementEnd::started(std::uint64_t start)
{ if (entry_ && (start < beforeStart_ || start > beforeStart_ + 1)) revoke(); }
void OrdinaryReplacementEnd::expire(std::int64_t now)
{ if (entry_ && (now < 0 || now >= deadline_)) revoke(); }
bool OrdinaryReplacementEnd::consume(std::int64_t entry, bool stop, std::uint64_t start, Identity identity, std::int64_t now)
{
  if (!entry_) return false;
  if (identity.epoch != identity_.epoch || identity.request != identity_.request || identity.binding != identity_.binding ||
      identity.revision != identity_.revision || now < 0 || now >= deadline_ || start < beforeStart_ || start > beforeStart_ + 1) {
    revoke(); return false;
  }
  if (entry != entry_ || !stop) return false;
  revoke(); return true;
}
void OrdinaryReplacementEnd::revoke() { entry_ = deadline_ = 0; beforeStart_ = 0; identity_ = {}; }
