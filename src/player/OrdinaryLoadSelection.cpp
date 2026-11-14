#include "OrdinaryLoadSelection.h"
#include <limits>

bool OrdinaryLoadSelection::begin(std::uint64_t replyId, std::uint64_t revision, std::uint64_t binding,
                                  std::uint64_t beforeStart, const QSet<std::int64_t>& beforeEntries,
                                  const QVariant& audio, const QVariant& subtitle)
{
  if (replyId_ || !replyId || !revision || !binding || beforeStart == std::numeric_limits<std::uint64_t>::max()) {
    revoke(); return false;
  }
  for (const auto id : beforeEntries) if (id <= 0) { revoke(); return false; }
  replyId_ = replyId; revision_ = revision; binding_ = binding; beforeStart_ = beforeStart;
  beforeEntries_ = beforeEntries; audio_ = audio; subtitle_ = subtitle; return true;
}
void OrdinaryLoadSelection::started(std::int64_t entry, std::uint64_t startSerial)
{
  if (!replyId_) return;
  if (entry <= 0 || entry_ || startSerial != beforeStart_ + 1 || beforeEntries_.contains(entry) ||
      (mappedEntry_ && mappedEntry_ != entry)) { revoke(); return; }
  entry_ = entry; startSerial_ = startSerial;
}
void OrdinaryLoadSelection::after(std::int64_t entry, std::uint64_t startSerial, bool uniqueCurrentAndUrlMatch)
{
  if (!replyId_) return;
  if (!entry_ || entry != entry_ || startSerial != startSerial_ || !uniqueCurrentAndUrlMatch || after_) { revoke(); return; }
  after_ = true;
}
void OrdinaryLoadSelection::reply(std::uint64_t replyId, bool successfulEntryless, std::int64_t mappedEntry)
{
  if (!replyId_ || replyId != replyId_) return;
  if (success_ || (!successfulEntryless && mappedEntry <= 0) || (entry_ && mappedEntry && entry_ != mappedEntry)) { revoke(); return; }
  mappedEntry_ = mappedEntry;
  success_ = true;
}
bool OrdinaryLoadSelection::revise(std::uint64_t previous, std::uint64_t revision, const QVariant& audio, const QVariant& subtitle)
{
  if (!replyId_ || revision_ != previous || revision <= previous) return false;
  revision_ = revision; audio_ = audio; subtitle_ = subtitle; return true;
}
void OrdinaryLoadSelection::ended(std::int64_t entry)
{
  if (replyId_ && (entry <= 0 || entry == entry_ || !beforeEntries_.contains(entry))) revoke();
}
void OrdinaryLoadSelection::loaded(std::int64_t entry, std::uint64_t startSerial)
{
  if (!replyId_) return;
  if (!entry_ || entry != entry_ || startSerial != startSerial_ || loaded_) { revoke(); return; }
  loaded_ = true;
}
bool OrdinaryLoadSelection::select(std::uint64_t revision, std::uint64_t binding, std::int64_t entry,
                                    std::uint64_t startSerial, bool requireLoaded, QVariant& audio, QVariant& subtitle) const
{
  if (!replyId_ || !success_ || !after_ || !entry_ || revision != revision_ || binding != binding_ || entry != entry_ ||
      startSerial != startSerial_ || (requireLoaded && !loaded_)) return false;
  audio = audio_; subtitle = subtitle_; return true;
}
void OrdinaryLoadSelection::revoke()
{
  replyId_ = revision_ = binding_ = beforeStart_ = startSerial_ = 0; entry_ = mappedEntry_ = 0;
  beforeEntries_.clear(); audio_ = {}; subtitle_ = {}; success_ = after_ = loaded_ = false;
}
