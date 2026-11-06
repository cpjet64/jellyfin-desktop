#pragma once
#include <cstdint>
#include <memory>
#include "module_gate.h"
namespace HealthyRetirement { class RetirementClaim; }

namespace HealthyModuleIdentity {
enum class SourceContract { Unqualified, BuilderSourceQualified };
enum class ModuleGateProgress { Pending, Ready, Failed, Unconfirmed };
class ChildModuleGate;
class ChildEndpoints;

// This capability records module identity and the accepted builder-source
// contract only. It does not authorize native free or establish live GL safety.
class VerifiedModule final {
public:
  ~VerifiedModule();
  VerifiedModule(const VerifiedModule&) = delete;
  VerifiedModule& operator=(const VerifiedModule&) = delete;
  bool stillValid(std::uint64_t generation) const noexcept;
  bool bindAdmissionClaim(const std::shared_ptr<HealthyRetirement::RetirementClaim>& claim,
                          std::uint64_t generation) const noexcept;
  bool admissionSnapshot(std::uint64_t generation) const noexcept;
  bool launchBinding(std::uint64_t generation, LaunchBinding& output) const noexcept;
  bool launchId(std::uint64_t generation, std::uint64_t& output) const noexcept;
#ifdef HEALTHY_MODULE_TESTING
  // Injects an observed loss only in the dedicated headless test build.
  void invalidateAdmissionForTest() const noexcept;
  // Dedicated accessor tests only; never present in the native fixture.
  static std::unique_ptr<VerifiedModule> forAccessorTest(std::uint64_t generation,
      std::uint64_t launch, bool (*valid)(void*) noexcept, void* context);
  static std::unique_ptr<VerifiedModule> forAccessorTest(std::uint64_t generation,
      std::uint64_t launch, const LaunchBinding& binding, bool (*valid)(void*) noexcept, void* context);
#endif
  SourceContract renderSourceContract(std::uint64_t generation) const noexcept;
  SourceContract wakeSourceContract(std::uint64_t generation) const noexcept;
private:
  friend class ChildModuleGate;
  friend class ChildEndpoints;
  bool acceptsEndpoint(std::uintptr_t pipe, std::uint64_t generation, std::uint64_t deadline) const noexcept;
  struct State;
  explicit VerifiedModule(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};

// The child bootstrap supplies the restricted inherited pipe and parent
// process credential from the fixture launch, not values decoded from a frame.
// Creation validates the live pipe peer and parent; input and I/O remain
// owned until completion or explicit safe containment of uncertain I/O.
class ChildModuleGate final {
public:
  static std::unique_ptr<ChildModuleGate> create(std::uintptr_t pipe, std::uintptr_t parentProcess,
      std::uint64_t expectedLaunch, std::uint64_t generation, std::uint64_t absoluteDeadline,
      std::uint64_t expectedRun, LaunchRole expectedRole);
  ~ChildModuleGate();
  ChildModuleGate(const ChildModuleGate&) = delete;
  ChildModuleGate& operator=(const ChildModuleGate&) = delete;
  ModuleGateProgress begin();
  ModuleGateProgress poll();
  ModuleGateProgress cancel();
  std::unique_ptr<VerifiedModule> take();
private:
  struct State;
  explicit ChildModuleGate(std::unique_ptr<State> state);
  ModuleGateProgress finish();
  std::unique_ptr<State> state_;
};
}
