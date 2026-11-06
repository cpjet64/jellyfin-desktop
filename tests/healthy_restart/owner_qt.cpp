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
  explicit QtRenderJob(std::shared_ptr<RenderJob> job) : job_(std::move(job)) {}
  void run() override { job_->run(); }
private:
  std::shared_ptr<RenderJob> job_;
};
}
QtScheduler::QtScheduler(QQuickWindow* window) : window_(window), timer_(new QTimer(this))
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
  window_->scheduleRenderJob(new QtRenderJob(std::move(job)), QQuickWindow::BeforeRenderingStage);
  window_->update();
  return true;
}
void QtScheduler::watch(QObject* dependent)
{
  if (!dependent) {
    if (const auto owner = owner_.lock()) owner->invalidate();
    return;
  }
  QObject::connect(dependent, &QObject::destroyed, this, [this] {
    if (const auto owner = owner_.lock()) owner->invalidate();
  }, Qt::DirectConnection);
}
}
