#pragma once
#include "lifetime.h"
#include <cstdint>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
class QThread;

namespace HealthyRetirement {
// The graph retains this object, item and callback storage through completion,
// including indefinitely when native operations cannot be confirmed.
class Closure {
public:
  enum class EventResult { Pending, Closed, Failed };
  enum class Phase { Initial, ClosingEvents, Joining, Destroying, Closed, Retained };
  struct Facts {
    std::uint64_t generation = 0;
    Phase phase = Phase::Initial;
    bool eventsClosed = false, quitRequested = false, joined = false;
    bool destroyInvoked = false, destroyReturned = false;
  };
  struct StopKey {
    enum class Origin { Invalid, InternalReplacement };
    Origin origin = Origin::Invalid;
    std::uint64_t host = 0, core = 0, epoch = 0, request = 0, revision = 0, attempt = 0;
    std::int64_t entry = 0;
    bool paused = false;
  };
  struct Operations {
    std::function<std::int64_t()> now;
    // Returns the actual Player's settled Stop key, never a caller assertion.
    std::function<std::optional<StopKey>()> settledStop;
    std::function<EventResult()> closeEvents;
    std::function<void()> quitWorker;
    // Runs on the retained non-GUI completion thread, with remaining time.
    std::function<bool(std::int64_t)> joinWorker;
    std::function<void()> destroyCore;
  };
  Closure(std::shared_ptr<Slot> slot, std::uint64_t generation,
          std::int64_t deadline, StopKey stop, std::shared_ptr<const std::atomic_bool> lease,
          Operations operations);
#ifdef HEALTHY_RETIREMENT_TESTING
  Closure(std::shared_ptr<Slot> slot, std::shared_ptr<Lifetime> testLifetime,
          std::uint64_t generation, std::int64_t deadline, StopKey stop, Operations operations);
#endif
  ~Closure();
  Closure(const Closure&) = delete;
  Closure& operator=(const Closure&) = delete;
  bool start();
  // Called synchronously by the owner when the bound intent is invalidated.
  void invalidate() noexcept;
  void poll();
  Facts facts() const;
  bool closed(std::uint64_t generation) const;
private:
  bool validTime();
  void complete();
  const std::shared_ptr<Slot> slot_;
#ifdef HEALTHY_RETIREMENT_TESTING
  std::shared_ptr<Lifetime> testLifetime_;
#endif
  const std::uint64_t generation_;
  const std::int64_t deadline_;
  const StopKey stop_;
  const std::shared_ptr<const std::atomic_bool> lease_;
  Operations ops_;
  mutable std::mutex mutex_;
  Facts facts_;
  std::int64_t lastTime_ = 0;
  QThread* worker_ = nullptr;
  std::atomic<bool> completionReturned_{false};
  std::atomic<bool> invalidated_{false};
  bool polling_ = false;
};
}
