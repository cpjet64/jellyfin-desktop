#include "graph_access.h"
#include <QThread>
#include <utility>

namespace HealthyRetirement {
namespace {
bool onOwner(const std::shared_ptr<GraphAccess>& access) noexcept
{
  return access && access->guiThread == std::this_thread::get_id() &&
         access->guiQtThread == QThread::currentThread();
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
FixtureGraph* GraphAccessOps::beginCleanup(const std::shared_ptr<GraphAccess>& access) noexcept
{
  auto* graph = graphOnOwner(access);
  if (!graph || access->revoked.load()) return nullptr;
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
  auto expected = AccessPhase::CleanupOnly;
  return access->phase.compare_exchange_strong(expected, AccessPhase::Contained);
}
void GraphAccessOps::invalidate(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access) return;
  auto phase = access->phase.load();
  while (phase != AccessPhase::Closed && phase != AccessPhase::Contained &&
         !access->phase.compare_exchange_weak(phase, AccessPhase::Contained)) {}
  if (phase == AccessPhase::Closed || phase == AccessPhase::Contained) return;
  access->revoked.store(true);
  if (const auto claim = std::atomic_load(&access->intentClaim)) claim->revoke();
  if (auto* graph = graphOnOwner(access)) {
    try { if (access->guiInvalidate) access->guiInvalidate(graph); } catch (...) {}
  }
}
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
