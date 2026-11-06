#pragma once
#include "lifetime.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

namespace HealthyRetirement {
// The fixture supplies an exclusive owner for the actual window, item,
// controller, event worker and their dependent objects. A wrapper around raw
// pointers does not satisfy this contract.
class Graph {
public:
  virtual ~Graph() = default;
  virtual bool eventWorkerClosed() const = 0;
  virtual bool approveDestruction() = 0;
};

// Constructed and retained by the outer process before exposing the graph.
// If that owner is lost unexpectedly, its preallocated state is deliberately
// left alive for process containment; destruction cannot allocate or tear down
// an uncertain graph. markLost() records observed destruction, not ownership.
class Containment {
public:
  Containment();
  ~Containment();
  bool adopt(std::unique_ptr<Graph>& graph, const std::shared_ptr<Slot>& slot,
             const std::shared_ptr<void>& token) noexcept;
  void markLost() noexcept { lost_.store(true); }
  bool intact() const noexcept { return intact_.load() && !lost_.load(); }
private:
  friend class Owner;
  struct State;
  std::unique_ptr<State> state_;
  std::atomic<bool> intact_{false};
  std::atomic<bool> lost_{false};
  std::atomic<bool> claimed_{false};
  bool adopted_ = false;
};

class Owner;
class RenderJob {
public:
  explicit RenderJob(std::shared_ptr<Owner> owner);
  ~RenderJob();
  void run();
private:
  std::shared_ptr<Owner> owner_;
  bool ran_ = false;
};

class Owner : public std::enable_shared_from_this<Owner> {
public:
  // Must be a nonblocking, nonreentrant, side-effect-free monotonic read in
  // the same units and epoch as the outer process's original deadline.
  using Clock = std::function<std::int64_t()>;
  using Schedule = std::function<bool(std::shared_ptr<RenderJob>)>;
  Owner(std::shared_ptr<Containment> containment, std::int64_t deadline,
        Clock clock, Schedule schedule);
#ifdef HEALTHY_RETIREMENT_TESTING
  // Compiled only into headless tests; native Slot::install remains unqualified.
  Owner(std::shared_ptr<Containment> containment, std::int64_t deadline,
        Clock clock, Schedule schedule, std::shared_ptr<Lifetime> testLifetime);
#endif
  ~Owner();
  bool request();
  void poll();
  void invalidate();
  bool acceptAndRelease();
  bool retained() const;
  bool renderThreadIs(std::uintptr_t thread) const;
private:
  friend class RenderJob;
  void runJob();
  void droppedJob();
  bool post();
  bool validTime();
  std::shared_ptr<Lifetime> lifetime() const;
  void fail();
  mutable std::mutex mutex_;
  std::shared_ptr<Containment> containment_;
  const std::int64_t deadline_;
  Clock clock_;
  Schedule schedule_;
#ifdef HEALTHY_RETIREMENT_TESTING
  std::shared_ptr<Lifetime> testLifetime_;
#endif
  std::int64_t lastTime_ = 0;
  bool claimed_ = false, requested_ = false, pending_ = false, failed_ = false, released_ = false, accepting_ = false;
};
}
