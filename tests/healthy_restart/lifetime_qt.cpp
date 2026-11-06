#include "lifetime_qt.h"
#include <QMetaObject>
#include <QObject>
#include <QOpenGLContext>
#include <utility>

namespace HealthyRetirement {
QtToken::QtToken(std::shared_ptr<Slot> slot, QObject* dispatcher, std::function<void()> delivery)
  : slot_(std::move(slot)), dispatcher_(dispatcher), delivery_(std::move(delivery)) {}

bool QtToken::watchContext(QOpenGLContext* context)
{
  if (!slot_ || !context) return false;
  const auto generation = slot_->generation();
  const auto invalidate = [weak = std::weak_ptr<Slot>(slot_), generation] {
    if (const auto owner = weak.lock()) owner->invalidate(generation);
  };
  const auto about = QObject::connect(context, &QOpenGLContext::aboutToBeDestroyed, invalidate);
  const auto destroyed = QObject::connect(context, &QObject::destroyed, invalidate);
  if (about && destroyed) return true;
  QObject::disconnect(about);
  QObject::disconnect(destroyed);
  slot_->invalidate(generation);
  return false;
}

void QtToken::redraw(void* opaque) noexcept
{
  if (opaque) static_cast<QtToken*>(opaque)->post();
}

void QtToken::post() noexcept
{
  if (!slot_ || !dispatcher_) return;
  const auto state = slot_->current();
  const auto generation = slot_->generation();
  if (!state || !state->enterCallback(generation)) return;
  try {
    const auto self = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(dispatcher_, [self, state, generation] {
      if (!state->enterDelivery(generation)) return;
      try {
        if (self->delivery_) self->delivery_();
      } catch (...) { self->slot_->invalidate(generation); }
      state->leaveDelivery();
    }, Qt::QueuedConnection);
    if (!queued) slot_->invalidate(generation);
  } catch (...) {
    slot_->invalidate(generation);
  }
  state->leaveCallback();
}
}
