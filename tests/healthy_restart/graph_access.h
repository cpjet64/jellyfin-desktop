#pragma once
#include "graph.h"

namespace HealthyRetirement {
class GraphAccessOps {
  friend class FixtureGraph;
  friend class PreparedGraph;
  static FixtureGraph* graphOnOwner(const std::shared_ptr<GraphAccess>& access) noexcept;
  static FixtureGraph* graphOnGui(const std::shared_ptr<GraphAccess>& access) noexcept;
  static FixtureGraph* beginCleanup(const std::shared_ptr<GraphAccess>& access, std::uint64_t generation) noexcept;
  static FixtureGraph* cleanupGraph(const std::shared_ptr<GraphAccess>& access) noexcept;
  static bool containCleanup(const std::shared_ptr<GraphAccess>& access) noexcept;
  static void invalidate(const std::shared_ptr<GraphAccess>& access) noexcept;
  static std::function<bool(std::uint64_t)> capability(const std::shared_ptr<GraphAccess>& access,
                                                       std::function<bool(std::uint64_t)> module);
#ifdef HEALTHY_RETIREMENT_TESTING
public:
  static void setContainPauseForTest(std::function<void()> pause);
  static FixtureGraph* graphOnGuiForTest(const std::shared_ptr<GraphAccess>& access) noexcept
  { return graphOnGui(access); }
  static void invalidateForTest(const std::shared_ptr<GraphAccess>& access) noexcept { invalidate(access); }
  static FixtureGraph* beginCleanupForTest(const std::shared_ptr<GraphAccess>& access, std::uint64_t generation) noexcept
  { return beginCleanup(access, generation); }
  static FixtureGraph* cleanupGraphForTest(const std::shared_ptr<GraphAccess>& access) noexcept
  { return cleanupGraph(access); }
  static bool containCleanupForTest(const std::shared_ptr<GraphAccess>& access) noexcept
  { return containCleanup(access); }
  static std::function<bool(std::uint64_t)> capabilityForTest(const std::shared_ptr<GraphAccess>& access,
                                                             std::function<bool(std::uint64_t)> module)
  { return capability(access, std::move(module)); }
#endif
};
}
