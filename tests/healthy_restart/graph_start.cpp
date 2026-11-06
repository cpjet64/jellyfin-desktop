#include "graph.h"
#include "module_endpoints.h"
#include "owner_qt.h"
#include "player/MpvVideoItem.h"
#include "player/PlayerComponent.h"
#include <MpvController>
#include <QAbstractEventDispatcher>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <utility>

namespace HealthyRetirement {
bool PreparedGraph::begin(std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints)
{
  auto* graph = graphOnGuiThread();
  return graph && graph->begin(containment_, std::move(endpoints));
}

bool FixtureGraph::begin(const std::shared_ptr<Containment>& containment,
                         std::shared_ptr<HealthyModuleIdentity::ChildEndpoints> endpoints)
{
  const auto generation = slot_->generation();
  const auto verified = endpoints ? endpoints->capability() : nullptr;
  if (!containment || !containment->intact() || module_ || window_ || !verified ||
      !qobject_cast<QGuiApplication*>(QCoreApplication::instance()) ||
      QQuickWindow::graphicsApi() != QSGRendererInterface::OpenGL ||
      !verified->stillValid(generation)) return false;
  std::int64_t started;
  try { started = clock_(); } catch (...) { containment->markLost(); return false; }
  if (started < 0 || started >= normalDeadline_) { containment->markLost(); return false; }
  try {
    endpoints_ = std::move(endpoints);
    module_ = verified;
    const auto revoked = std::shared_ptr<const std::atomic_bool>(access_, &access_->revoked);
    if (!slot_->bindModule(module_, revoked)) { containment->markLost(); return false; }
    containment_ = containment;
    window_ = std::make_unique<QQuickWindow>();
    scheduler_ = std::make_unique<QtScheduler>(window_.get(), containment);
    const auto access = access_;
    owner_ = std::make_shared<Owner>(containment, deadline_, clock_, [access](std::shared_ptr<RenderJob> job) {
      const auto graph = access->graph.load();
      return graph && graph->scheduler_ && graph->scheduler_->schedule(std::move(job));
    });
    scheduler_->watch(dispatcher_.get());
    player_ = std::make_unique<PlayerComponent>();
    scheduler_->watch(player_.get());
    item_ = new MpvVideoItem(window_->contentItem());
    item_->setParent(window_->contentItem());
    scheduler_->watch(item_);
    auto* controller = item_->controller();
    auto* worker = controller ? controller->thread() : nullptr;
    const auto now = clock_();
    if (now < started || now >= normalDeadline_ || !module_->stillValid(generation) || !controller ||
        !controller->mpv() || !worker || worker == QThread::currentThread() || !worker->isRunning() ||
        !worker->eventDispatcher() || !item_->healthyBindToken(token_)) {
      owner_->invalidate(); return false;
    }
    if (!player_->healthyPrepareWake(generation, *module_, dispatcher_.get()) || !player_->componentInitialize()) {
      owner_->invalidate(); return false;
    }
    item_->setObjectName(QStringLiteral("video"));
    player_->setWindow(window_.get());
    const auto boundAt = clock_();
    if (!containment->intact() || !item_ || player_->m_mpv != controller ||
        !module_->stillValid(generation) || boundAt < now || boundAt >= normalDeadline_) {
      owner_->invalidate(); return false;
    }
    return true;
  } catch (...) {
    if (owner_) owner_->invalidate(); else containment->markLost();
    return false;
  }
}

bool FixtureGraph::present()
{
  const auto containment = containment_.lock();
  if (!window_ || !item_ || !player_ || !owner_ || !module_ || retirementRequested_ || presented_ ||
      !containment || !containment->intact() || window_->thread() != QThread::currentThread() ||
      !module_->stillValid(slot_->generation())) return false;
  try {
    const auto before = clock_();
    if (before < 0 || before >= normalDeadline_) { owner_->invalidate(); return false; }
    presented_ = true;
    window_->resize(640, 360);
    item_->setSize(QSizeF(640, 360));
    window_->show();
    const auto after = clock_();
    if (after >= before && after < normalDeadline_ && containment->intact() &&
        module_->stillValid(slot_->generation())) return true;
    owner_->invalidate();
    return false;
  } catch (...) { owner_->invalidate(); return false; }
}
}
