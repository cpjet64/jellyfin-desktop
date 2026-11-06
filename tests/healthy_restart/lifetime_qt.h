#pragma once
#include "lifetime.h"
#include <QObject>
#include <functional>
#include <memory>
class QOpenGLContext;

namespace HealthyRetirement {
// The fixture owner retains this token and dispatcher until native callback
// quiescence and all queued deliveries have settled, including on failure.
class QtToken : public std::enable_shared_from_this<QtToken> {
public:
  QtToken(std::shared_ptr<Slot> slot, QObject* dispatcher, std::function<void()> delivery);
  static void redraw(void* opaque) noexcept;
  bool watchContext(QOpenGLContext* context);
  std::shared_ptr<Slot> slot() const { return slot_; }
  bool valid() const { return slot_ && dispatcher_ && static_cast<bool>(delivery_); }
private:
  void post() noexcept;
  std::shared_ptr<Slot> slot_;
  QObject* const dispatcher_;
  std::function<void()> delivery_;
};
}
