#pragma once
#include "AutomaticRecoverySession.h"
#include <cstdint>

// Pure policy: every input must be an independently completed native batch.
class AutomaticRecoveryReadStall {
public:
  enum class Result { None, BufferingStall };
  struct Sample {
    AutomaticRecoverySession::Key key;
    std::int64_t entry = 0, observedMs = -1, nowMs = -1, positionMs = -1;
    std::uint64_t eventEpoch = 0, batchSequence = 0, sourceFrameSerial = 0;
    bool nativePaused = false, desiredPaused = false, seekable = false, video = false;
    bool loading = false, seeking = false, buffering = false, eof = false, permanentFailure = false, errorAbsent = false;
    bool visible = false, exposed = false;
  };
  Result observe(const Sample& sample);
  void reset();
private:
  Sample previous_;
  std::uint64_t lastSequence_ = 0;
  std::int64_t baselineMs_ = 0;
  bool active_ = false;
};
