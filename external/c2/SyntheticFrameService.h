#pragma once

#include <cstdint>
#include <mutex>

// Diagnostic-only permission; ordinary rendering remains closed after failure.
class SyntheticFrameService
{
public:
  enum class Phase { Never, Armed, Active, Closing, WorkerClosed, Closed, Failed };
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
    if (generation != generation_ || phase_ != Phase::Armed) return false;
    if (!synthetic || error >= 0 || !original || now >= deadline_) { phase_ = Phase::Failed; return false; }
    phase_ = Phase::Active;
    return true;
  }

  bool allow(std::uint64_t generation, bool original, std::int64_t now)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (generation != generation_ || phase_ != Phase::Active) return false;
    if (!original || now >= deadline_) { phase_ = Phase::Failed; return false; }
    return true;
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
};
