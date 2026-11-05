#pragma once

#include "healthy_cleanup_coordinator.h"
#include <QObject>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

// QtCore-only producer binding. The caller must retain targets and the binding
// if any operation is blocked or uncertain; destruction is not a fallback.
// A failed setter does not prove producer closure: deliveries that observe the
// failure are rejected, but already-running work may remain. Callback state
// and its owner must remain alive for outer containment.
// Both roles use one exact native handle's single callback slot: Controller is
// installed first, then replaced by Player. They are not separate sources.
class HealthyWakeBinding
{
public:
  enum class Role { Controller, Player };
  enum class Result { Done, Blocked, Unsupported };
  enum class NativeBarrier { Unqualified, SingleSlotSerialized };
  struct Key { std::uint64_t owner = 0, generation = 0; };
  using NativeCallback = void (*)(void*);
  using Setter = std::function<void(NativeCallback, void*)>;
  using Delivery = std::function<void(Role)>;
  // Clock and absolute deadlines use the same monotonic nanosecond epoch.
  using Clock = std::function<std::int64_t()>;

  // SingleSlotSerialized asserts that the native setter and every invocation
  // hold the same native lock from before callback entry until callback return.
  // Its provenance must be independently established for the selected binary.
  // A deadline cannot interrupt a blocked synchronous setter. Bounded native
  // calls and the existing outer containment remain adapter qualifications.
  // Controller installation runs on its worker; Player installation and apply
  // run on the player/GUI thread. The owner serializes these transitions.
  HealthyWakeBinding(QObject* controllerTarget, QObject* playerTarget, QObject* hookTarget, QObject* worker,
                     Key key, Setter setter, Delivery delivery, Clock clock, NativeBarrier barrier);
  ~HealthyWakeBinding();
  HealthyWakeBinding(const HealthyWakeBinding&) = delete;
  HealthyWakeBinding& operator=(const HealthyWakeBinding&) = delete;

  // Only Controller may be installed directly. A valid handoff has one attempt:
  // close its delivery gate, fence the worker, replace the same native slot and
  // queue one Player kick. Failure keeps the gate closed and owner retained.
  // A wrong key or already-expired deadline is rejected before starting an
  // attempt and does not change the valid owner's gate.
  // The eventual adapter must reuse finishMpvEventSetup's existing kick, not
  // add this modeled kick as a second startup notification.
  bool install(Role role) noexcept;
  Result handoff(Key key, std::int64_t deadline) noexcept;
  // Guard copied item-to-controller work independently of consumer handoff.
  bool controllerWorkAllowed(Key key) const noexcept;
  // Action and delivery captures must own their dependencies. Deliveries can
  // run concurrently on controller and player threads; shared captures need
  // synchronization. Work posted by delivery or action must carry the same
  // owner/generation guard: the worker fence cannot drain work posted behind it.
  // Targets remain alive until all barriers and fixture ownership are settled.
  bool postHook(int delayMs, std::function<void()> action) noexcept;
  Result apply(HealthyCleanup::Step step, Key key, std::int64_t deadline) noexcept;

private:
  struct State;
  struct Endpoint;
  static void wake(void* context) noexcept;
  static bool postDelivery(const std::shared_ptr<State>& state, Role role);
  Result fence(std::int64_t deadline) noexcept;
  bool onOwnerThread() const noexcept;
  std::shared_ptr<State> state_;
  std::vector<std::unique_ptr<Endpoint>> endpoints_;
  Setter setter_;
  Clock clock_;
};
