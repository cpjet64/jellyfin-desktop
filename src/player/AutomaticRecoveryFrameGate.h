#pragma once
#include "AutomaticRecoverySession.h"
#include "native/RecoveryFrameObservation.h"
#include <cstdint>

// Only renderer-owned native and Qt completion observations may enter this gate.
class AutomaticRecoveryFrameGate {
public:
  using Key = AutomaticRecoverySession::Key;
  static constexpr std::int64_t PositionToleranceMs = 2000;
  bool begin(Key key, std::uint64_t attempt, std::int64_t entry, std::int64_t targetMs,
             std::uint64_t epoch, std::uint64_t previousSourceSerial,
             std::int64_t nowMs, std::int64_t deadlineMs);
  bool rendered(Key key, std::uint64_t attempt, std::uint64_t epoch, const RecoveryFrameObservationV1& frame, std::int64_t nowMs);
  bool submitted(Key key, std::uint64_t attempt, std::uint64_t epoch, std::uint64_t renderSerial, std::int64_t nowMs);
  bool swapped(Key key, std::uint64_t attempt, std::uint64_t epoch, std::uint64_t renderSerial, std::int64_t nowMs);
  bool position(Key key, std::uint64_t attempt, std::int64_t entry, std::int64_t positionMs,
                bool nativePaused, std::uint64_t epoch, std::int64_t nowMs);
  bool ready(Key key, std::uint64_t attempt, std::uint64_t epoch, std::int64_t nowMs) const;
  void invalidate();
private:
  bool matches(Key key, std::uint64_t attempt, std::uint64_t epoch, std::int64_t nowMs) const;
  bool nearTarget(std::int64_t value) const;
  Key key_;
  std::uint64_t attempt_ = 0, epoch_ = 0, discontinuity_ = 0, previousSource_ = 0, renderSerial_ = 0;
  std::int64_t entry_ = 0, target_ = 0, deadline_ = 0, lastMs_ = 0, positionMsAt_ = 0;
  bool active_ = false, submitted_ = false, swapped_ = false, positioned_ = false;
};
