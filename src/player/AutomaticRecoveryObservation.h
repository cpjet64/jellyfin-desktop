#pragma once
#include "AutomaticRecoverySession.h"
#include <cstdint>
#include <functional>
#include <mpv/client.h>

// Serialized by the existing control-thread event consumer.
class AutomaticRecoveryObservation {
public:
  using Key = AutomaticRecoverySession::Key;
  using Dispatch = std::function<int(std::uint64_t, const char*, mpv_format)>;
  struct Result {
    std::int64_t entry = 0, positionMs = -1, observedMs = -1;
    std::uint64_t batchSequence = 0;
    bool seekable = false, seeking = false, buffering = false, nativePaused = false, video = false;
  };
  bool begin(Key key, std::int64_t entry, std::uint64_t eventEpoch, std::int64_t nowMs, const Dispatch& dispatch);
  void observe(const mpv_event* event, Key current, std::uint64_t eventEpoch, std::int64_t nowMs,
               const Dispatch& dispatch);
  bool take(Key current, std::uint64_t eventEpoch, std::int64_t nowMs, Result& result);
  bool ready() const { return phase_ == Phase::Ready; }
  bool failed() const { return phase_ == Phase::Failed; }
  void invalidate();
private:
  enum class Phase { Idle, Before, Payload, After, Ready, Failed };
  bool current(Key key, std::uint64_t epoch, std::int64_t nowMs) const;
  bool request(const Dispatch& dispatch, unsigned slot, const char* name, mpv_format format);
  bool requestPayload(const Dispatch& dispatch);
  bool consumePayload(unsigned slot, const mpv_event_property& property);
  std::uint64_t userdata(unsigned slot) const;
  Key key_;
  Result result_;
  Phase phase_ = Phase::Idle;
  std::uint64_t sequence_ = 0, epoch_ = 0;
  std::int64_t startedMs_ = 0, lastMs_ = 0;
  unsigned pending_ = 0;
};
