#pragma once
#include "graph.h"

namespace HealthyRetirement {
class GraphAccessOps {
  friend class FixtureGraph;
  friend class PreparedGraph;
  static FixtureGraph* graphOnGui(const std::shared_ptr<GraphAccess>& access) noexcept;
  static void invalidate(const std::shared_ptr<GraphAccess>& access) noexcept;
  static std::function<bool(std::uint64_t)> capability(const std::shared_ptr<GraphAccess>& access,
                                                       std::function<bool(std::uint64_t)> module);
#ifdef HEALTHY_RETIREMENT_TESTING
public:
  static FixtureGraph* graphOnGuiForTest(const std::shared_ptr<GraphAccess>& access) noexcept
  { return graphOnGui(access); }
  static void invalidateForTest(const std::shared_ptr<GraphAccess>& access) noexcept { invalidate(access); }
  static std::function<bool(std::uint64_t)> capabilityForTest(const std::shared_ptr<GraphAccess>& access,
                                                             std::function<bool(std::uint64_t)> module)
  { return capability(access, std::move(module)); }
#endif
};
}
