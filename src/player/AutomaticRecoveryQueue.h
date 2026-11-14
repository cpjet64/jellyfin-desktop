#pragma once
#include "AutomaticRecoverySession.h"
#include <cstdint>
#include <map>

// Control-thread-only recipes. No URL, header, or track data is logged.
class AutomaticRecoveryQueue {
public:
  using Key = AutomaticRecoverySession::Key;
  using Snapshot = AutomaticRecoverySession::Snapshot;
  struct Expired { std::uint64_t sequence = 0; Key key; };
  bool enqueue(std::uint64_t sequence, Key key, const Snapshot& recipe, std::int64_t deadlineMs);
  bool reply(std::uint64_t sequence, std::int64_t entry, std::int64_t nowMs);
  void reject(std::uint64_t sequence);
  void start(std::int64_t entry, std::int64_t nowMs);
  void loaded(std::int64_t nowMs);
  void end(std::int64_t entry, std::int64_t nowMs);
  bool position(Key key, std::int64_t entry, std::int64_t positionMs, std::int64_t nowMs);
  bool snapshot(Key current, Snapshot& output) const;
  bool reviseActive(Key previous, Key current, bool paused);
  bool updateTracks(Key current, const QVariant& audio, const QVariant& subtitle);
  bool replaceActiveEntry(Key current, std::int64_t previous, std::int64_t entry, std::int64_t positionMs);
  bool selections(std::int64_t entry, Key current, QVariant& audio, QVariant& subtitle) const;
  bool poll(std::int64_t nowMs);
  bool takeExpired(Expired& output);
  Key activeKey() const { return active_.key(); }
  std::int64_t activeEntry() const { return active_.entry(); }
  void clear();
private:
  struct Pending { Key key; AutomaticRecoverySession session; std::int64_t deadline = 0, entry = 0; bool expired = false; };
  bool observeTime(std::int64_t nowMs);
  void promote();
  std::map<std::uint64_t, Pending> pending_;
  AutomaticRecoverySession active_;
  std::uint64_t lastSequence_ = 0;
  std::int64_t startedEntry_ = 0;
  std::int64_t lastEventTime_ = -1;
  bool loaded_ = false;
};
