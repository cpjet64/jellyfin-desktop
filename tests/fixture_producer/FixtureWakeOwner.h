#pragma once

#include "HealthyWakeBinding.h"
#include <functional>
#include <memory>
#include <atomic>

class MpvController;
class QObject;

// Fixture retains this instance and its QObject targets on every blocked path.
// This adapter binds only the wake/event/hook operations; render retirement and
// owned destruction remain unsupported and no native fixture may be launched.
// The native setter and monotonic clock must not pump a nested GUI event loop.
class FixtureWakeOwner
{
public:
  using Result = HealthyWakeBinding::Result;
  using Key = HealthyWakeBinding::Key;
  enum class Phase { New, ControllerInstalled, PlayerInstalled, Blocked };
  FixtureWakeOwner(QObject* player, Key key, std::int64_t deadline,
                   HealthyWakeBinding::NativeBarrier barrier);
  ~FixtureWakeOwner();
  FixtureWakeOwner(const FixtureWakeOwner&) = delete;
  FixtureWakeOwner& operator=(const FixtureWakeOwner&) = delete;
  bool installController(MpvController* controller) noexcept;
  Result handoff() noexcept;
  bool controllerWorkAllowed() const noexcept;
  bool postHook(int delayMs, std::function<void()> action) noexcept;
  Result apply(HealthyCleanup::Step step) noexcept;
  Phase phase() const noexcept { return phase_.load(); }
private:
  QObject* player_;
  Key key_;
  std::int64_t deadline_;
  HealthyWakeBinding::NativeBarrier barrier_;
  std::unique_ptr<HealthyWakeBinding> binding_;
  std::atomic<Phase> phase_{Phase::New};
};
