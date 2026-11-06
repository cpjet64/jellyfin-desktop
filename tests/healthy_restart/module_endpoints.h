#pragma once
#include "module_channel.h"
#include "../fixture_restart/WindowsRestartIo.h"
#include <cstdint>
#include <memory>
#include <vector>
#ifdef HEALTHY_ENDPOINT_TESTING
#include <functional>
#endif

namespace HealthyModuleIdentity {
enum class EndpointMode { Source, Restore };
enum class EndpointRole { Evidence, Grant, Control, Ack };
#ifdef HEALTHY_ENDPOINT_TESTING
enum class EndpointComparison { Same, Different, Error };
#endif
struct EndpointInput { EndpointRole role; std::uintptr_t inheritedPipe; };

// The controlled child bootstrap supplies roles; wire data never chooses them.
// The bundle owns the capability and duplicated endpoints until I/O settles.
class ChildEndpoints final {
public:
#if defined(_WIN32) && defined(HEALTHY_ENDPOINT_TESTING)
  void invalidateAdmissionForTest() noexcept;
  void setAdmissionBindHookForTest(void (*hook)(void*) noexcept, void* context) noexcept;
  void setAdmissionBeforePublishHookForTest(void (*hook)(void*) noexcept, void* context) noexcept;
  struct TestOperations {
    std::function<bool()> valid;
    std::function<bool(std::uintptr_t)> accepts;
    std::function<EndpointComparison(std::uintptr_t, std::uintptr_t)> compare;
    std::function<bool(std::uintptr_t, HANDLE&)> duplicate;
    std::function<bool(HANDLE)> close;
    FixtureRestart::WindowsIoApi io;
  };
  static std::unique_ptr<ChildEndpoints> createForTest(std::uint64_t deadline, EndpointMode mode,
      const EndpointInput (&inputs)[3], const TestOperations& operations);
  static std::unique_ptr<ChildEndpoints> createForTest(std::unique_ptr<VerifiedModule>& module,
      std::uint64_t generation, std::uint64_t deadline, EndpointMode mode,
      const EndpointInput (&inputs)[3], const TestOperations& operations);
#endif
  static std::unique_ptr<ChildEndpoints> create(std::unique_ptr<VerifiedModule>& module,
      std::uint64_t generation, std::uint64_t deadline, EndpointMode mode,
      const EndpointInput (&inputs)[3]);
  ~ChildEndpoints();
  ChildEndpoints(const ChildEndpoints&) = delete;
  ChildEndpoints& operator=(const ChildEndpoints&) = delete;
  const void* token(EndpointRole role) const noexcept;
  // The returned ownership does not attest current validity. Recheck
  // VerifiedModule::stillValid with the owning generation at each boundary.
  std::shared_ptr<const VerifiedModule> capability() const noexcept;
  bool bindAdmissionClaim(const std::shared_ptr<HealthyRetirement::RetirementClaim>& claim,
                          std::uint64_t generation) noexcept;
  bool admissionSnapshot(std::uint64_t generation, const void* control, const void* ack) const noexcept;
#ifdef _WIN32
  void beginContainment() noexcept;
  FixtureRestart::IoProgress begin(EndpointRole role, bool read, const void* data, DWORD size);
  FixtureRestart::IoProgress beginTrackedRead(EndpointRole role, DWORD size);
  std::uint64_t trackedReadOperation(EndpointRole role) const noexcept;
  std::shared_ptr<const FixtureRestart::WindowsRestartIo::Settlement> trackedReadSettlement(EndpointRole role) const noexcept;
  bool consumeTrackedReadSettlement(EndpointRole role, std::uint64_t operation);
  FixtureRestart::IoProgress beginTrackedGrantRead(DWORD size);
  std::uint64_t grantReadOperation() const noexcept;
  std::shared_ptr<const FixtureRestart::WindowsRestartIo::Settlement> grantReadSettlement() const noexcept;
  bool consumeGrantReadSettlement(std::uint64_t operation);
  FixtureRestart::IoProgress poll(EndpointRole role);
  FixtureRestart::IoProgress cancel(EndpointRole role);
  std::shared_ptr<const FixtureRestart::WindowsRestartIo::Settlement> retainedSettlement(EndpointRole role) const noexcept;
  bool consumeRetainedSettlement(EndpointRole role, std::uint64_t operation);
  bool containmentUncertain(EndpointRole role) const noexcept;
  bool closeSettled(EndpointRole role);
  bool consumeCompletedWrite(EndpointRole role);
  bool canceledWithoutData(EndpointRole role) const;
  const std::vector<unsigned char>* bytes(EndpointRole role) const noexcept;
#endif
private:
  struct State;
#ifdef _WIN32
  struct Operations;
  static std::unique_ptr<ChildEndpoints> createImpl(std::unique_ptr<VerifiedModule>& module,
      std::uint64_t generation, std::uint64_t deadline, EndpointMode mode,
      const EndpointInput (&inputs)[3], Operations operations);
#endif
  explicit ChildEndpoints(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}
