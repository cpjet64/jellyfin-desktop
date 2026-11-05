#include "FixtureWakeOwner.h"
#include <MpvController>
#include <QMetaObject>
#include <QThread>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace {
std::int64_t monotonicNanos() noexcept
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
    std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

FixtureWakeOwner::FixtureWakeOwner(QObject* player, Key key, std::int64_t deadline,
                                   HealthyWakeBinding::NativeBarrier barrier)
  : player_(player), key_(key), deadline_(deadline), barrier_(barrier)
{
}

FixtureWakeOwner::~FixtureWakeOwner() = default;

bool FixtureWakeOwner::installController(MpvController* controller) noexcept
{
  try {
    if (binding_ || !player_ || !controller || !controller->mpv() ||
        controller->thread() != QThread::currentThread() || monotonicNanos() >= deadline_) {
      phase_ = Phase::Blocked;
      return false;
    }
    auto setter = [controller](HealthyWakeBinding::NativeCallback callback, void* context) {
      mpv_set_wakeup_callback(controller->mpv(), callback, context);
    };
    auto delivery = [controller, player = player_](HealthyWakeBinding::Role role) {
      if (role == HealthyWakeBinding::Role::Controller) controller->eventHandler();
      else if (!QMetaObject::invokeMethod(player, "handleMpvEvents", Qt::DirectConnection))
        throw std::runtime_error("fixture player event handler unavailable");
    };
    binding_.reset(new HealthyWakeBinding(controller, player_, player_, controller, key_,
      std::move(setter), std::move(delivery), monotonicNanos, barrier_));
    const bool installed = binding_->install(HealthyWakeBinding::Role::Controller);
    phase_ = installed ? Phase::ControllerInstalled : Phase::Blocked;
    return installed;
  } catch (...) { phase_ = Phase::Blocked; return false; }
}

FixtureWakeOwner::Result FixtureWakeOwner::handoff() noexcept
{
  const auto result = binding_ ? binding_->handoff(key_, deadline_) : Result::Blocked;
  phase_ = result == Result::Done ? Phase::PlayerInstalled : Phase::Blocked;
  return result;
}

bool FixtureWakeOwner::controllerWorkAllowed() const noexcept
{
  return binding_ && binding_->controllerWorkAllowed(key_);
}

bool FixtureWakeOwner::postHook(int delayMs, std::function<void()> action) noexcept
{
  const bool posted = binding_ && binding_->postHook(delayMs, std::move(action));
  if (!posted) phase_ = Phase::Blocked;
  return posted;
}

FixtureWakeOwner::Result FixtureWakeOwner::apply(HealthyCleanup::Step step) noexcept
{
  return binding_ ? binding_->apply(step, key_, deadline_) : Result::Blocked;
}
