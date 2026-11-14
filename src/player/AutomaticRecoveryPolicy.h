#pragma once
#include <cstdint>

namespace AutomaticRecovery {
enum class Action { None, Reload, Retire, Cancel, Blocked };
enum class Phase { Idle, Reloading, Retiring, Restoring, WaitingFrame, Complete, Failed };
struct Sample {
  std::int64_t nowMs = 0, positionMs = -1;
  std::uint64_t session = 0, revision = 0, generation = 0, frameSerial = 0;
  bool seekable = false, video = false, loading = false, seeking = false, buffering = false;
  bool visible = false, exposed = false, eof = false, outstanding = false, operationTimedOut = false;
  bool rendererFault = false, paused = false;
};

class Policy {
public:
  Action observe(const Sample& sample);
  bool retired(std::uint64_t original, std::uint64_t replacement, std::int64_t nowMs);
  // Call only after the identified load and its post-seek state are qualified.
  bool restoreStarted(std::uint64_t acceptedLoadAttempt, std::uint64_t frameSerialBeforeRestore, std::int64_t nowMs);
  bool frameObserved(std::uint64_t generation, std::uint64_t loadAttempt, std::uint64_t frameSerial, std::int64_t nowMs);
  bool mayUnpause(const Sample& sample) const;
  void fail();
  Phase phase() const { return phase_; }
  std::int64_t deadlineMs() const { return deadline_; }
  bool attempted() const { return attempted_; }
private:
  void resetBaseline(const Sample& sample);
  bool active() const;
  bool inTime(std::int64_t now) const;
  Phase phase_ = Phase::Idle;
  std::uint64_t session_ = 0, revision_ = 0, generation_ = 0, replacement_ = 0;
  std::uint64_t baselineFrame_ = 0;
  std::uint64_t baselineRevision_ = 0, baselineGeneration_ = 0;
  std::uint64_t loadAttempt_ = 0, restoreFrameSerial_ = 0;
  std::int64_t baselineTime_ = 0, baselinePosition_ = -1, deadline_ = 0;
  std::int64_t lastPosition_ = -1, lastAdvanceTime_ = 0;
  std::int64_t lastAttemptTime_ = 0;
  bool attempted_ = false, baseline_ = false, baselineOutstanding_ = false, wasPaused_ = false;
};
}
