#include "owner_qt.h"
#include <QQuickWindow>
#include <QRunnable>
#include <QThread>
#include <QTimer>
#include <utility>

namespace HealthyRetirement {
namespace {
class QtRenderJob final : public QRunnable {
public:
  QtRenderJob(std::shared_ptr<RenderJob> job, QThread* guiThread)
    : job_(std::move(job)), guiThread_(guiThread) {}
  void run() override { if (QThread::currentThread() != guiThread_) job_->run(); }
private:
  std::shared_ptr<RenderJob> job_;
  QThread* const guiThread_;
};
}
QtScheduler::QtScheduler(QQuickWindow* window, const std::shared_ptr<Containment>& containment)
  : window_(window), containment_(containment), timer_(new QTimer(this))
{
  timer_->setInterval(50);
  QObject::connect(timer_, &QTimer::timeout, this, [this] {
    if (const auto owner = owner_.lock()) owner->poll();
  });
  watch(window);
}
void QtScheduler::bind(const std::shared_ptr<Owner>& owner)
{
  owner_ = owner;
  timer_->start();
}
bool QtScheduler::schedule(std::shared_ptr<RenderJob> job)
{
  if (!job || !window_ || QThread::currentThread() != thread() || !window_->isExposed()) return false;
  const auto owner = owner_.lock();
  if (!owner || owner->renderThreadIs(reinterpret_cast<std::uintptr_t>(QThread::currentThreadId()))) return false;
  window_->scheduleRenderJob(new QtRenderJob(std::move(job), thread()), QQuickWindow::BeforeRenderingStage);
  window_->update();
  return true;
}
void QtScheduler::watch(QObject* dependent)
{
  if (!dependent) {
    if (const auto containment = containment_.lock()) containment->markLost();
    if (const auto owner = owner_.lock()) owner->invalidate();
    return;
  }
  QObject::connect(dependent, &QObject::destroyed, this, [this] {
    if (const auto containment = containment_.lock()) containment->markLost();
    if (const auto owner = owner_.lock()) owner->invalidate();
  }, Qt::DirectConnection);
}
}
