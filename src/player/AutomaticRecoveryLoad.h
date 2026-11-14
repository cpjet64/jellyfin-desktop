#pragma once
#include <cstdint>

// One owned load request; caller must serialize it with the sole mpv consumer.
class AutomaticRecoveryLoad {
public:
  struct Key { std::uint64_t session = 0, revision = 0, generation = 0; };
  enum class State { Empty, Pending, Accepted, Failed, Expired };
  static constexpr std::uint64_t Tag = (std::uint64_t{3} << 61);
  static constexpr std::uint64_t MaxSequence = (std::uint64_t{1} << 53) - 1;
  bool begin(Key key, std::uint64_t sequence, std::int64_t deadlineMs);
  void reply(std::uint64_t userdata, bool ok, std::int64_t entry, std::int64_t nowMs);
  void started(std::int64_t entry, std::int64_t nowMs);
  void ended(std::int64_t entry);
  State poll(Key current, std::int64_t nowMs);
  void invalidate();
  State state() const { return state_; }
  std::int64_t entry() const { return state_ == State::Accepted ? replyEntry_ : 0; }
  std::uint64_t userdata() const { return Tag | sequence_; }
private:
  bool matches(Key current) const;
  bool observeTime(std::int64_t nowMs);
  void settle();
  Key key_;
  State state_ = State::Empty;
  std::uint64_t sequence_ = 0;
  std::int64_t deadline_ = 0, replyEntry_ = 0, startedEntry_ = 0;
  std::int64_t lastEventTime_ = -1;
  bool replySeen_ = false, startSeen_ = false;
};
