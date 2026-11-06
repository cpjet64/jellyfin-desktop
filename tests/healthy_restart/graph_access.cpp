#include "graph_access.h"
#include <QThread>
#include <utility>

namespace HealthyRetirement {
namespace {
#ifdef HEALTHY_RETIREMENT_TESTING
std::function<void()> containPause;
#endif
bool onOwner(const std::shared_ptr<GraphAccess>& access) noexcept
{
  return access && access->guiThread == std::this_thread::get_id() &&
         access->guiQtThread == QThread::currentThread();
}
void containClaim(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (const auto claim = std::atomic_load(&access->intentClaim)) claim->contain();
}
}
FixtureGraph* GraphAccessOps::graphOnOwner(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!onOwner(access)) return nullptr;
  return access->graph.load();
}
FixtureGraph* GraphAccessOps::graphOnGui(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access || access->phase.load() != AccessPhase::Normal) return nullptr;
  return graphOnOwner(access);
}
FixtureGraph* GraphAccessOps::beginCleanup(const std::shared_ptr<GraphAccess>& access, std::uint64_t generation) noexcept
{
  auto* graph = graphOnOwner(access);
  if (!graph || !generation || access->revoked.load()) return nullptr;
  const auto phase = access->phase.load();
  if (phase != AccessPhase::Normal && phase != AccessPhase::CleanupOnly) return nullptr;
  const auto claim = std::atomic_load(&access->intentClaim);
  if (claim && claim->generation() != generation) return nullptr;
  if (claim && !claim->enterCleanup(generation) && !claim->recognizeCleanup(generation)) {
    claim->contain();
    auto expected = phase;
    access->phase.compare_exchange_strong(expected, AccessPhase::Contained);
    return nullptr;
  }
  auto expected = AccessPhase::Normal;
  if (!access->phase.compare_exchange_strong(expected, AccessPhase::CleanupOnly) &&
      expected != AccessPhase::CleanupOnly) return nullptr;
  return graph;
}
FixtureGraph* GraphAccessOps::cleanupGraph(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access || access->phase.load() != AccessPhase::CleanupOnly || access->revoked.load()) return nullptr;
  return graphOnOwner(access);
}
bool GraphAccessOps::containCleanup(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!onOwner(access)) return false;
  if (access->phase.load() != AccessPhase::CleanupOnly) return false;
  containClaim(access);
  auto expected = AccessPhase::CleanupOnly;
  const bool changed = access->phase.compare_exchange_strong(expected, AccessPhase::Contained);
  if (changed) containClaim(access);
  return changed;
}
void GraphAccessOps::invalidate(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access) return;
  auto phase = access->phase.load();
  if (phase == AccessPhase::Closed || phase == AccessPhase::Contained) return;
  containClaim(access);
#ifdef HEALTHY_RETIREMENT_TESTING
  try { if (containPause) containPause(); } catch (...) {}
#endif
  while (phase != AccessPhase::Closed && phase != AccessPhase::Contained &&
         !access->phase.compare_exchange_weak(phase, AccessPhase::Contained)) {}
  if (phase == AccessPhase::Closed || phase == AccessPhase::Contained) return;
  access->revoked.store(true);
  containClaim(access);
  if (auto* graph = graphOnOwner(access)) {
    try { if (access->guiInvalidate) access->guiInvalidate(graph); } catch (...) {}
  }
}
#ifdef HEALTHY_RETIREMENT_TESTING
void GraphAccessOps::setContainPauseForTest(std::function<void()> pause)
{
  containPause = std::move(pause);
}
#endif
std::function<bool(std::uint64_t)> GraphAccessOps::capability(const std::shared_ptr<GraphAccess>& access,
                                                             std::function<bool(std::uint64_t)> module)
{
  const std::weak_ptr<GraphAccess> weak = access;
  return [weak, module = std::move(module)](std::uint64_t generation) {
    const auto state = weak.lock();
    if (!state || state->revoked.load()) return false;
    const auto phase = state->phase.load();
    return (phase == AccessPhase::Normal || phase == AccessPhase::CleanupOnly) && module && module(generation);
  };
}
}
