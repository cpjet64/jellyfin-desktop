#pragma once

#include <cstdint>
#include <mutex>

// Diagnostic-only permission; ordinary rendering remains closed after failure.
class SyntheticFrameService
{
public:
  enum class Phase { Never, Armed, Active, Closing, WorkerClosed, Closed, Failed };
  enum class Rejection { None, Generation, Phase, Context, Deadline };
  struct Telemetry {
    std::uint64_t activationGeneration = 0;
    std::int64_t activationNow = 0, deadline = 0;
    int activationError = 0;
    bool activationEvaluated = false, activationSynthetic = false, activationOriginal = false, activationResult = false;
    std::uint64_t allowCount = 0;
    Rejection firstRejection = Rejection::None, lastRejection = Rejection::None;
  };
  explicit SyntheticFrameService(std::uint64_t generation) : generation_(generation) {}

  bool arm(std::uint64_t generation, std::int64_t deadline, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != Phase::Never || deadline <= now) return false;
    deadline_ = deadline;
    phase_ = Phase::Armed;
    return true;
  }

  bool activate(std::uint64_t generation, bool synthetic, int error, bool original, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    telemetry_.activationEvaluated = true;
    telemetry_.activationGeneration = generation;
    telemetry_.activationNow = now;
    telemetry_.deadline = deadline_;
    telemetry_.activationError = error;
    telemetry_.activationSynthetic = synthetic;
    telemetry_.activationOriginal = original;
    bool accepted = false;
    if (generation == generation_ && phase_ == Phase::Armed) {
      if (!synthetic || error >= 0 || !original || now >= deadline_) phase_ = Phase::Failed;
      else { phase_ = Phase::Active; accepted = true; }
    }
    telemetry_.activationResult = accepted;
    return accepted;
  }

  bool allow(std::uint64_t generation, bool original, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    Rejection reason = Rejection::None;
    if (generation != generation_) reason = Rejection::Generation;
    else if (phase_ != Phase::Active) reason = Rejection::Phase;
    else if (!original) { reason = Rejection::Context; phase_ = Phase::Failed; }
    else if (now >= deadline_) { reason = Rejection::Deadline; phase_ = Phase::Failed; }
    ++telemetry_.allowCount;
    if (reason != Rejection::None) {
      if (telemetry_.firstRejection == Rejection::None) telemetry_.firstRejection = reason;
      telemetry_.lastRejection = reason;
    }
    return reason == Rejection::None;
  }

  Telemetry telemetry()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return telemetry_;
  }

  void result(std::uint64_t generation, int code)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation == generation_ && (phase_ == Phase::Active || phase_ == Phase::Closing) && code < 0)
      phase_ = Phase::Failed;
  }

  bool requestClose(std::uint64_t generation, bool terminalConfirmed, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != Phase::Active || !terminalConfirmed) return false;
    if (now >= deadline_) { phase_ = Phase::Failed; return false; }
    phase_ = Phase::Closing;
    return true;
  }

  bool close(std::uint64_t generation, bool original, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != Phase::Closing) return false;
    if (!original || now >= deadline_) { phase_ = Phase::Failed; return false; }
    phase_ = Phase::WorkerClosed;
    return true;
  }

  bool acknowledge(std::uint64_t generation)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != Phase::WorkerClosed) return false;
    phase_ = Phase::Closed;
    return true;
  }

  bool retirementAllowed(std::uint64_t generation)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return generation == generation_ && (phase_ == Phase::Never || phase_ == Phase::Closed);
  }

  Phase phase()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return phase_;
  }

private:
  const std::uint64_t generation_;
  std::mutex mutex_;
  std::int64_t deadline_ = 0;
  Phase phase_ = Phase::Never;
  Telemetry telemetry_;
};

// The callbacks contain the existing operations; each is invoked at most once.
template<class Allow, class Update, class Skip>
void serviceSyntheticFrame(Allow allow, Update update, Skip skip, std::uint64_t frameFlag)
{
  if (!allow()) return;
  if (!(update() & frameFlag)) return;
  skip();
}
