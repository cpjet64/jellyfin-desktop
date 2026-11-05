#pragma once

#include <cstdint>

// Control-thread state only. Values are bounded for lossless JavaScript transport;
// no transport or qualified producer identity is established by this class.
class PlaybackIntent
{
public:
  static constexpr std::uint64_t MaxSafeInteger = 9007199254740991ULL;
  enum class Desired { Stopped, Playing, Paused, Exiting };
  enum class Reason { Initial, LegacyUnclassified, WebStopUnclassified, NativeStop,
                      RendererLost, UnattributedEvent, UnsupportedCommand, CounterExhausted, Exiting };
  enum class Dispatch { None, GuardRejected, Queued, NativeAccepted, NativeRejected, Unavailable };
  enum class Admission { None, Unclassified, Accepted, Rejected };
  enum class Observation { Unknown };
  struct Snapshot {
    std::uint64_t epoch = 0, request = 0, revision = 0;
    Desired desired = Desired::Stopped;
    Reason reason = Reason::Initial;
    bool hasSeek = false;
    std::int64_t seekMs = 0;
    bool timelineQualified = false;
    Dispatch dispatch = Dispatch::None;
    Admission admission = Admission::None;
    Observation observation = Observation::Unknown;
  };

  explicit PlaybackIntent(std::uint64_t epoch) noexcept;
  void play(bool paused) noexcept;
  void stop(Reason reason) noexcept;
  void pause() noexcept;
  void resume() noexcept;
  void seek(std::int64_t milliseconds) noexcept;
  void invalidate(Reason reason) noexcept;
  void exit() noexcept;
  bool dispatch(std::uint64_t revision, Dispatch result) noexcept;
  Snapshot snapshot() const noexcept { return state_; }
  bool automaticResumeEligible() const noexcept { return false; }

private:
  bool advance(bool newRequest) noexcept;
  Snapshot state_;
};
