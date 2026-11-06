#include "graph.h"
#include "failure_cause.h"
#include "graph_retirement.h"
#include "graph_access.h"
#include "graph_session.h"
#include "owner_qt.h"
#include "module_endpoints.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include <QCoreApplication>
#include <QQuickWindow>
#include <QThread>
#include <utility>

namespace HealthyRetirement {
FixtureGraph::FixtureGraph(std::shared_ptr<Slot> slot, std::shared_ptr<GraphAccess> access,
                           std::int64_t normalDeadline, std::int64_t deadline, Clock clock)
  : slot_(std::move(slot)), access_(std::move(access)), normalDeadline_(normalDeadline), deadline_(deadline),
    clock_(std::move(clock)), dispatcher_(std::make_unique<QObject>())
{
}

std::unique_ptr<PreparedGraph> FixtureGraph::prepare(std::uint64_t generation,
                                                    std::int64_t normalDeadline,
                                                    std::int64_t deadline, Clock clock)
{
  const auto app = QCoreApplication::instance();
  if (!app || app->thread() != QThread::currentThread() || !generation || !clock) return {};
  try {
    const auto now = clock();
    constexpr std::int64_t reserveNs = 30000000000LL;
    if (now < 0 || now >= normalDeadline || normalDeadline >= deadline ||
        deadline - normalDeadline < reserveNs) return {};
  } catch (...) { return {}; }
  auto prepared = std::make_unique<PreparedGraph>();
  prepared->containment_ = std::make_shared<Containment>();
  prepared->slot_ = std::make_shared<Slot>(generation, deadline, clock);
  prepared->access_ = std::make_shared<GraphAccess>();
  prepared->access_->guiThread = std::this_thread::get_id();
  prepared->access_->guiQtThread = QThread::currentThread();
  prepared->access_->guiInvalidate = [](FixtureGraph* graph) { graph->invalidate(); };
  auto concrete = std::unique_ptr<FixtureGraph>(new FixtureGraph(prepared->slot_, prepared->access_, normalDeadline,
                                                                 deadline, std::move(clock)));
  const auto access = prepared->access_;
  concrete->token_ = std::make_shared<QtToken>(prepared->slot_, concrete->dispatcher_.get(), [access] {
    if (auto* graph = access->graph.load()) graph->deliverRedraw();
  });
  auto* graph = concrete.get();
  std::unique_ptr<Graph> base = std::move(concrete);
  if (!prepared->containment_->adopt(base, prepared->slot_, graph->token_)) return {};
  access->graph.store(graph);
  return prepared;
}

FixtureGraph* PreparedGraph::graphOnGuiThread() const noexcept
{
  return GraphAccessOps::graphOnGui(access_);
}

std::optional<ClosedAck> PreparedGraph::closedAck() const
{
  return GraphRetirement::readAck(access_);
}
bool PreparedGraph::ownsEndpoints(const std::shared_ptr<HealthyModuleIdentity::ChildEndpoints>& endpoints) const noexcept
{
  return access_ && endpoints && access_->endpoints.lock() == endpoints;
}

void PreparedGraph::invalidate() noexcept
{
  GraphAccessOps::invalidate(access_);
}

bool PreparedGraph::beginRestoreFailure(HealthyRestart::FailureCause cause)
{
  const auto access = access_;
  const auto retained = containment_;
  if (!retained || cause == HealthyRestart::FailureCause::None) return false;
  auto* graph = GraphAccessOps::beginCleanup(access);
  if (!graph) return false;
  try {
    if (graph->beginRestoreFailure(cause) && access->phase.load() == AccessPhase::CleanupOnly)
      return true;
  } catch (...) {}
  GraphAccessOps::containCleanup(access);
  return false;
}

PreparedGraph::FailureProgress PreparedGraph::pollRestoreFailure()
{
  const auto access = access_;
  const auto retained = containment_;
  if (!retained) return FailureProgress::Unknown;
  auto* graph = GraphAccessOps::cleanupGraph(access);
  if (!graph) return FailureProgress::Unknown;
  try {
    const auto result = graph->pollRestoreFailure();
    if (access->phase.load() != AccessPhase::CleanupOnly) return FailureProgress::Unknown;
    if (result == FailureProgress::Pending || result == FailureProgress::Qualified) return result;
  } catch (...) {}
  GraphAccessOps::containCleanup(access);
  return FailureProgress::Unknown;
}

void FixtureGraph::deliverRedraw()
{
  if (item_) item_->update();
}

FixtureGraph::~FixtureGraph()
{
  access_->graph.store(nullptr);
}
}
