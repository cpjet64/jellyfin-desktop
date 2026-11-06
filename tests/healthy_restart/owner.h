#pragma once
#include "lifetime.h"
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
  virtual bool intact() const = 0;
  virtual bool eventWorkerClosed() const = 0;
  virtual bool approveDestruction() = 0;
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
  using Clock = std::function<std::int64_t()>;
  using Schedule = std::function<bool(std::shared_ptr<RenderJob>)>;
  Owner(std::unique_ptr<Graph> graph, std::shared_ptr<Slot> slot,
        std::shared_ptr<void> token, std::int64_t deadline, Clock clock,
        Schedule schedule);
#ifdef HEALTHY_RETIREMENT_TESTING
  // Compiled only into headless tests; native Slot::install remains unqualified.
  Owner(std::unique_ptr<Graph> graph, std::shared_ptr<Slot> slot,
        std::shared_ptr<void> token, std::int64_t deadline, Clock clock,
        Schedule schedule, std::shared_ptr<Lifetime> testLifetime);
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
  std::unique_ptr<Graph> graph_;
  std::shared_ptr<Slot> slot_;
  std::shared_ptr<void> token_;
  const std::int64_t deadline_;
  Clock clock_;
  Schedule schedule_;
#ifdef HEALTHY_RETIREMENT_TESTING
  std::shared_ptr<Lifetime> testLifetime_;
#endif
  std::int64_t lastTime_ = 0;
  bool requested_ = false, pending_ = false, failed_ = false, released_ = false, accepting_ = false;
};
}
