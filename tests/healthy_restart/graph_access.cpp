#include "graph_access.h"
#include <QThread>
#include <utility>

namespace HealthyRetirement {
FixtureGraph* GraphAccessOps::graphOnGui(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access || access->guiThread != std::this_thread::get_id() ||
      access->guiQtThread != QThread::currentThread()) return nullptr;
  return access->graph.load();
}
void GraphAccessOps::invalidate(const std::shared_ptr<GraphAccess>& access) noexcept
{
  if (!access) return;
  access->revoked.store(true);
  if (const auto claim = std::atomic_load(&access->intentClaim)) claim->revoke();
  if (auto* graph = graphOnGui(access)) {
    try { if (access->guiInvalidate) access->guiInvalidate(graph); } catch (...) {}
  }
}
std::function<bool(std::uint64_t)> GraphAccessOps::capability(const std::shared_ptr<GraphAccess>& access,
                                                             std::function<bool(std::uint64_t)> module)
{
  const std::weak_ptr<GraphAccess> weak = access;
  return [weak, module = std::move(module)](std::uint64_t generation) {
    const auto state = weak.lock();
    return state && !state->revoked.load() && module && module(generation);
  };
}
}
