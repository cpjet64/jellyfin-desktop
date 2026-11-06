#pragma once
#include "graph.h"
#include "closure.h"

namespace HealthyRetirement {
// Production construction is restricted to FixtureGraph. The test factory
// substitutes native boundaries while exercising this same progression.
class GraphRetirement : public std::enable_shared_from_this<GraphRetirement> {
public:
  struct Boundaries {
    Closure::Operations closure;
    std::function<std::shared_ptr<const std::atomic_bool>()> stopLease;
    std::function<bool(std::shared_ptr<const std::atomic_bool>)> bindStopLease;
    std::function<bool(std::uint64_t)> capability;
    std::function<void()> bindScheduler;
  };
#ifdef HEALTHY_RETIREMENT_TESTING
  static std::shared_ptr<GraphRetirement> forTest(std::shared_ptr<Slot> slot,
      std::shared_ptr<Owner> owner, std::shared_ptr<Lifetime> lifetime, std::shared_ptr<GraphAccess> access,
      std::int64_t deadline, Boundaries boundaries);
#endif
  bool request();
  FixtureGraph::Progress poll();
  bool closed() const;
  std::optional<ClosedAck> ack() const;
  static std::optional<ClosedAck> readAck(const std::shared_ptr<GraphAccess>& access);
  const std::shared_ptr<Closure>& closure() const { return closure_; }
private:
  friend class FixtureGraph;
  GraphRetirement(std::shared_ptr<Slot> slot, std::shared_ptr<Owner> owner,
                  std::shared_ptr<GraphAccess> access, std::int64_t deadline, Boundaries boundaries);
  std::shared_ptr<Lifetime> lifetime() const;
  FixtureGraph::Progress publish();
  std::shared_ptr<Slot> slot_;
  std::shared_ptr<Owner> owner_;
  std::shared_ptr<GraphAccess> access_;
  const std::int64_t deadline_;
  Boundaries boundaries_;
  std::optional<Closure::StopKey> stop_;
  std::shared_ptr<const std::atomic_bool> lease_;
  std::shared_ptr<Closure> closure_;
#ifdef HEALTHY_RETIREMENT_TESTING
  std::shared_ptr<Lifetime> testLifetime_;
#endif
};
}
