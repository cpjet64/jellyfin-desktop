#pragma once
#include <QSet>
#include <QVariant>
#include <cstdint>

// Ordinary track choices only; this never establishes a recovery recipe.
class OrdinaryLoadSelection {
public:
  bool begin(std::uint64_t replyId, std::uint64_t revision, std::uint64_t binding, std::uint64_t beforeStart,
             const QSet<std::int64_t>& beforeEntries, const QVariant& audio, const QVariant& subtitle);
  void started(std::int64_t entry, std::uint64_t startSerial);
  void ended(std::int64_t entry);
  void reply(std::uint64_t replyId, bool successfulEntryless, std::int64_t mappedEntry = 0);
  bool revise(std::uint64_t previousRevision, std::uint64_t revision, const QVariant& audio, const QVariant& subtitle);
  void after(std::int64_t entry, std::uint64_t startSerial, bool uniqueCurrentAndUrlMatch);
  void loaded(std::int64_t entry, std::uint64_t startSerial);
  bool select(std::uint64_t revision, std::uint64_t binding, std::int64_t entry, std::uint64_t startSerial,
              bool requireLoaded, QVariant& audio, QVariant& subtitle) const;
  void revoke();
  bool active() const { return replyId_ != 0; }
private:
  QSet<std::int64_t> beforeEntries_;
  std::uint64_t replyId_ = 0, revision_ = 0, binding_ = 0, beforeStart_ = 0, startSerial_ = 0;
  std::int64_t entry_ = 0, mappedEntry_ = 0;
  QVariant audio_, subtitle_;
  bool success_ = false, after_ = false, loaded_ = false;
};
