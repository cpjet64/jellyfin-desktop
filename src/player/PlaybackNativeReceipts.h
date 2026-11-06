#pragma once

#include <array>
#include <cstdint>

// Control-thread only. Events remain unassociated until the command reply
// identifies the exact playlist entry. No position is inferred from mpv's
// global property notifications.
class PlaybackNativeReceipts
{
public:
  struct Key { std::uint64_t host = 0, core = 0, revision = 0, attempt = 0; };
  struct Snapshot {
    Key key;
    bool submissionObserved = false, submitted = false, reply = false, replyOk = false, started = false;
    bool ended = false, settled = false, invalid = false, consumed = false;
    std::int64_t entry = 0;
  };

  bool begin(Key key) noexcept;
  bool consume(Key current) noexcept;
  void submitted(int result) noexcept;
  void reply(std::uint64_t attempt, bool ok, std::int64_t entry) noexcept;
  void start(std::int64_t entry) noexcept;
  void end(std::int64_t entry, bool redirect) noexcept;
  void invalidate() noexcept;
  Snapshot snapshot() const noexcept { return state_; }

private:
  struct Event { std::int64_t entry = 0; bool start = false, end = false, redirect = false; };
  Event* event(std::int64_t entry) noexcept;
  void match() noexcept;
  Snapshot state_;
  std::array<Event, 16> events_{};
};
