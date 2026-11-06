#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include "retirement_claim.h"
namespace HealthyModuleIdentity { class VerifiedModule; }

namespace HealthyRetirement {
class FixtureGraph;
class CaptureBridge;
class CaptureController;
struct ClaimState { std::shared_ptr<const RetirementClaim> claim; };
struct Identity {
  std::uintptr_t thread = 0;
  std::uintptr_t context = 0;
  std::uint64_t epoch = 0;
};
enum class Barrier { Unqualified, TestQualified };
enum class BarrierResult { Ready, Waiting, Rejected };
enum class Phase { Active, Closing, Freeing, Acknowledged, Retained };
enum class Result { Pending, Done, Blocked };
enum class CaptureAvailability { Pending, Ready, Rejected };
struct Operations {
  // now is nonblocking, non-reentrant and uses the original monotonic epoch.
  std::function<std::int64_t()> now;
  std::function<Identity()> currentIdentity;
  std::function<void(void*)> unregisterCallback;
  std::function<BarrierResult()> barrier;
  std::function<bool(std::uint64_t)> invalidateQueued;
  std::function<void(void*)> freeContext;
  std::function<bool(void*)> clearPointer;
};
struct Facts {
  Phase phase = Phase::Active;
  bool admissionClosed = false, unregisterInvoked = false, unregisterReturned = false;
  bool barrierQualified = false, queuedInvalidated = false, freeInvoked = false;
  bool freeReturned = false, pointerCleared = false, ackPublished = false, ackAccepted = false;
  std::uint32_t callbacks = 0, renders = 0, deliveries = 0;
};
struct Ack {
  const std::uint64_t generation;
  const Identity identity;
  const std::int64_t completedAt;
};

// The real diagnostic constructor uses the default Unqualified barrier.
// An externally owned shared Lifetime and native callback storage must survive
// every callback, render operation and queued job, including uncertain cases.
class Lifetime {
public:
  Lifetime(std::uint64_t generation, Identity original, void* renderContext,
           std::int64_t overallDeadline, Operations operations,
           Barrier barrier = Barrier::Unqualified, std::int64_t minimumTime = 0);
  bool enterRender(std::uint64_t generation);
  void leaveRender();
  bool enterCallback(std::uint64_t generation);
  void leaveCallback();
  bool deliveryAllowed(std::uint64_t generation) const;
  bool enterDelivery(std::uint64_t generation);
  void leaveDelivery();
  Result request(std::uint64_t generation);
  Result retireOnRenderThread(std::uint64_t generation);
  void invalidate(std::uint64_t generation);
  std::optional<Ack> ack(std::uint64_t generation) const;
  bool acceptAck(std::uint64_t generation);
  bool mayDestroy(std::uint64_t generation, bool eventWorkerClosed) const;
  Identity originalIdentity() const { return original_; }
  bool matchesClaim(const std::shared_ptr<const RetirementClaim>& claim) const noexcept;
#ifdef HEALTHY_RETIREMENT_TESTING
  bool bindClaimForTest(std::shared_ptr<const RetirementClaim> claim);
#endif
  Facts facts() const;
private:
  friend class Slot;
  Lifetime(std::uint64_t generation, Identity original, void* renderContext,
           std::int64_t overallDeadline, Operations operations, std::int64_t minimumTime,
           std::function<bool()> moduleQualification, std::shared_ptr<ClaimState> claimState);
  bool matches(Identity value) const;
  bool timeValid() const;
  Result retireSteps();
  Result retain();
  const std::uint64_t generation_;
  const Identity original_;
  void* const context_;
  const std::int64_t deadline_;
  const Barrier barrier_;
  Operations ops_;
  std::function<bool()> moduleQualification_;
  std::shared_ptr<ClaimState> claimState_;
  mutable std::mutex mutex_;
  mutable Facts facts_;
  bool retiring_ = false, ownerValid_ = true;
  mutable std::int64_t lastTime_ = 0;
  std::optional<Ack> ack_;
};

// A fixture owner creates and retains this before the item can expose a
// context or register its callback. install() is the native construction path:
// it cannot opt in to the test-only qualified barrier.
class Slot {
public:
  // The same nonblocking monotonic clock/epoch is used through the entire attempt.
  Slot(std::uint64_t generation, std::int64_t overallDeadline,
       std::function<std::int64_t()> clock);
  bool beginCreation();
  bool beforeCreate();
  bool recordCreated(void* renderContext);
  bool hasContextRecord() const;
  std::shared_ptr<Lifetime> install(Identity original, void* renderContext, Operations operations);
  void creationFailed();
  Result request(std::uint64_t generation);
  std::shared_ptr<Lifetime> current() const;
  std::shared_ptr<CaptureBridge> captureBridge() const;
  bool captureAdmission(const std::shared_ptr<Lifetime>& lifetime);
  bool captureSnapshot(const std::shared_ptr<Lifetime>& lifetime) const;
  CaptureAvailability captureAvailability() const;
  void invalidate(std::uint64_t generation);
  std::uint64_t generation() const { return generation_; }
#ifdef HEALTHY_RETIREMENT_TESTING
  bool bindModuleForTest(std::function<bool()> capability, std::shared_ptr<const std::atomic_bool> revoked);
  bool bindStopClaimForTest(std::shared_ptr<const RetirementClaim> claim);
#endif
private:
  friend class FixtureGraph;
  friend class CaptureController;
  friend class CaptureBridge;
  friend class ::PlayerComponent;
  bool admitLoadDispatch(const std::shared_ptr<const RetirementClaim>& claim,
                         RetirementClaim::ArmIdentity identity) const;
  bool admitCaptureDispatch(const std::shared_ptr<Lifetime>& lifetime,
                            const std::shared_ptr<const RetirementClaim>& claim,
                            RetirementClaim::ArmIdentity identity) const;
  bool admitDispatchLocked(const std::shared_ptr<Lifetime>& lifetime,
                           const std::shared_ptr<const RetirementClaim>& claim,
                           RetirementClaim::ArmIdentity identity, bool seek) const;
  bool bindCaptureBridge(const std::shared_ptr<CaptureBridge>& bridge);
  bool bindModule(std::shared_ptr<const HealthyModuleIdentity::VerifiedModule> module,
                  std::shared_ptr<const std::atomic_bool> revoked);
  bool bindStopClaim(std::shared_ptr<const RetirementClaim> claim);
  bool bindGuard(std::function<bool()> capability, std::shared_ptr<const std::atomic_bool> revoked);
  const std::uint64_t generation_;
  const std::int64_t deadline_;
  const std::function<std::int64_t()> clock_;
  mutable std::mutex mutex_;
  bool closed_ = false, creating_ = false, captureBound_ = false;
  std::int64_t lastTime_ = 0;
  void* retainedContext_ = nullptr;
  std::shared_ptr<Lifetime> lifetime_;
  std::function<bool()> moduleQualification_;
  std::shared_ptr<ClaimState> claimState_;
  std::weak_ptr<CaptureBridge> captureBridge_;
};
}
