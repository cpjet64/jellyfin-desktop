#pragma once
#include "owner.h"
#include <QObject>
#include <QPointer>
#include <memory>
class QQuickWindow;
class QTimer;

namespace HealthyRetirement {
// Lives on the window's GUI thread. The outer process independently enforces
// the same absolute deadline if the GUI or render thread cannot progress.
class QtScheduler : public QObject {
public:
  QtScheduler(QQuickWindow* window, const std::shared_ptr<Containment>& containment);
  void bind(const std::shared_ptr<Owner>& owner);
  bool schedule(std::shared_ptr<RenderJob> job);
  void watch(QObject* dependent);
private:
  QPointer<QQuickWindow> window_;
  std::weak_ptr<Containment> containment_;
  std::weak_ptr<Owner> owner_;
  QTimer* timer_;
};
}
