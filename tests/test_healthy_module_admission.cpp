#include "healthy_restart/module_endpoints.h"
#include "healthy_restart/retirement_claim.h"
#include <cstdlib>
#include <memory>

using namespace HealthyModuleIdentity;
using HealthyRetirement::RetirementClaim;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

namespace {
int moduleCalls = 0, endpointCalls = 0;
bool moduleLive = true, endpointLive = true;
DWORD ioError = ERROR_IO_PENDING;
int cancelMode = 0;
const VerifiedModule* hookModule = nullptr;
ChildEndpoints* hookEndpoints = nullptr;
bool validModule(void*) noexcept {
  ++moduleCalls;
  if (hookEndpoints) { auto* target = hookEndpoints; hookEndpoints = nullptr; target->invalidateAdmissionForTest(); }
  return moduleLive;
}
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(99); }
BOOL WINAPI closeEvent(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return 1; }
BOOL WINAPI reset(HANDLE) { return TRUE; }
BOOL WINAPI pendingRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)
{ ioError = ERROR_IO_PENDING; return FALSE; }
BOOL WINAPI cancelIo(HANDLE, LPOVERLAPPED) { return TRUE; }
BOOL WINAPI resultIo(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{ *count = 0; ioError = cancelMode == 1 ? ERROR_ACCESS_DENIED : cancelMode == 2 ? ERROR_BROKEN_PIPE : ERROR_OPERATION_ABORTED; return FALSE; }
DWORD WINAPI lastError() { return ioError; }

ChildEndpoints::TestOperations operations()
{
  ChildEndpoints::TestOperations result;
  result.valid = [] {
    ++endpointCalls;
    if (hookModule) { auto* target = hookModule; hookModule = nullptr; target->invalidateAdmissionForTest(); }
    return endpointLive;
  };
  result.accepts = [](std::uintptr_t value) { return value != 0; };
  result.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a == b ? EndpointComparison::Same : EndpointComparison::Different;
  };
  result.duplicate = [](std::uintptr_t value, HANDLE& out) { out = reinterpret_cast<HANDLE>(value + 100); return true; };
  result.close = [](HANDLE) { return true; };
  result.io.createEvent = event; result.io.closeHandle = closeEvent; result.io.now = now;
  result.io.resetEvent = reset; result.io.readFile = pendingRead; result.io.cancel = cancelIo;
  result.io.result = resultIo; result.io.error = lastError;
  return result;
}

struct Fixture {
  std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(7);
  std::unique_ptr<VerifiedModule> module = VerifiedModule::forAccessorTest(7, 42, validModule, nullptr);
  std::unique_ptr<ChildEndpoints> endpoints;
  std::shared_ptr<const VerifiedModule> held;
  const void* control = nullptr;
  const void* ack = nullptr;
  Fixture() {
    const EndpointInput input[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    endpoints = ChildEndpoints::createForTest(module, 7, 100, EndpointMode::Restore, input, operations());
    CHECK(endpoints); held = endpoints->capability(); CHECK(held && !module);
    control = endpoints->token(EndpointRole::Control); ack = endpoints->token(EndpointRole::Ack);
    CHECK(control && ack && endpoints->bindAdmissionClaim(claim, 7));
  }
};

void snapshotsAreRetainedAndCallbackFree()
{
  moduleLive = endpointLive = true;
  Fixture fixture;
  CHECK(fixture.endpoints->bindAdmissionClaim(fixture.claim, 7));
  auto other = std::make_shared<RetirementClaim>(7);
  CHECK(!fixture.endpoints->bindAdmissionClaim(other, 7) && other->open());
  const int moduleBefore = moduleCalls, endpointBefore = endpointCalls;
  CHECK(fixture.held->admissionSnapshot(7));
  CHECK(fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
  CHECK(!fixture.endpoints->admissionSnapshot(7, fixture.ack, fixture.control));
  CHECK(!fixture.endpoints->admissionSnapshot(8, fixture.control, fixture.ack));
  CHECK(moduleCalls == moduleBefore && endpointCalls == endpointBefore);
}

void observedModuleLossRevokes()
{
  moduleLive = endpointLive = true;
  Fixture fixture;
  fixture.held->invalidateAdmissionForTest();
  CHECK(!fixture.claim->open() && !fixture.held->admissionSnapshot(7));
  CHECK(!fixture.held->stillValid(7));
  auto other = std::make_shared<RetirementClaim>(7);
  CHECK(!fixture.held->bindAdmissionClaim(other, 7) && other->open());
}

void observedEndpointLossRevokes()
{
  moduleLive = endpointLive = true;
  Fixture fixture;
  fixture.endpoints->invalidateAdmissionForTest();
  CHECK(!fixture.claim->open() && fixture.claim->contained());
  CHECK(!fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
  CHECK(!fixture.endpoints->token(EndpointRole::Ack));
}

void callbackLossIsLatched()
{
  moduleLive = endpointLive = true;
  Fixture fixture;
  endpointLive = false;
  CHECK(!fixture.endpoints->token(EndpointRole::Ack));
  endpointLive = true;
  CHECK(!fixture.claim->open() && !fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
}

void lastCallbacksCannotHideLoss()
{
  moduleLive = endpointLive = true;
  {
    Fixture fixture;
    hookModule = fixture.held.get();
    CHECK(fixture.endpoints->token(EndpointRole::Ack));
    CHECK(!fixture.held->admissionSnapshot(7) && !fixture.claim->open());
  }
  {
    Fixture fixture;
    hookEndpoints = fixture.endpoints.get();
    CHECK(fixture.held->stillValid(7));
    CHECK(!fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack) && !fixture.claim->open());
  }
}

void cancellationLossAndEmptyAbort()
{
  moduleLive = endpointLive = true;
  {
    Fixture fixture; cancelMode = 1;
    CHECK(fixture.endpoints->begin(EndpointRole::Control, true, nullptr, 1) == FixtureRestart::IoProgress::Pending);
    CHECK(fixture.endpoints->cancel(EndpointRole::Control) == FixtureRestart::IoProgress::Unconfirmed);
    CHECK(!fixture.claim->open() && !fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
    cancelMode = 0;
    (void)fixture.endpoints->cancel(EndpointRole::Control);
    const auto retained = fixture.endpoints->retainedSettlement(EndpointRole::Control);
    if (retained) CHECK(fixture.endpoints->consumeRetainedSettlement(EndpointRole::Control, retained->operation));
  }
  {
    Fixture fixture; cancelMode = 0;
    CHECK(fixture.endpoints->begin(EndpointRole::Control, true, nullptr, 1) == FixtureRestart::IoProgress::Pending);
    CHECK(fixture.endpoints->cancel(EndpointRole::Control) == FixtureRestart::IoProgress::Failed);
    CHECK(fixture.endpoints->canceledWithoutData(EndpointRole::Control) && fixture.claim->open());
    CHECK(fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
  }
  {
    Fixture fixture; cancelMode = 2;
    CHECK(fixture.endpoints->begin(EndpointRole::Control, true, nullptr, 1) == FixtureRestart::IoProgress::Pending);
    CHECK(fixture.endpoints->cancel(EndpointRole::Control) == FixtureRestart::IoProgress::Failed);
    CHECK(!fixture.claim->open() && !fixture.endpoints->admissionSnapshot(7, fixture.control, fixture.ack));
    cancelMode = 0;
  }
}

void loseModule(void* context) noexcept
{ static_cast<VerifiedModule*>(context)->invalidateAdmissionForTest(); }
void loseEndpoints(void* context) noexcept
{ static_cast<ChildEndpoints*>(context)->invalidateAdmissionForTest(); }

void bindingLossDoesNotWaitOrPublish()
{
  moduleLive = endpointLive = true;
  {
    auto claim = std::make_shared<RetirementClaim>(7);
    auto module = VerifiedModule::forAccessorTest(7, 42, validModule, nullptr);
    module->setAdmissionBindHookForTest(loseModule, module.get());
    CHECK(!module->bindAdmissionClaim(claim, 7));
    CHECK(!claim->open() && !module->admissionSnapshot(7));
  }
  {
    auto claim = std::make_shared<RetirementClaim>(7);
    auto module = VerifiedModule::forAccessorTest(7, 42, validModule, nullptr);
    const EndpointInput input[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
    auto endpoints = ChildEndpoints::createForTest(module, 7, 100, EndpointMode::Restore, input, operations());
    CHECK(endpoints);
    const auto control = endpoints->token(EndpointRole::Control), ack = endpoints->token(EndpointRole::Ack);
    endpoints->setAdmissionBindHookForTest(loseEndpoints, endpoints.get());
    CHECK(!endpoints->bindAdmissionClaim(claim, 7));
    CHECK(!claim->open() && !endpoints->admissionSnapshot(7, control, ack));
  }
}

void containmentBeforeClaimPublication()
{
  moduleLive = endpointLive = true;
  auto claim = std::make_shared<RetirementClaim>(7);
  auto module = VerifiedModule::forAccessorTest(7, 42, validModule, nullptr);
  const EndpointInput input[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  auto endpoints = ChildEndpoints::createForTest(module, 7, 100, EndpointMode::Restore, input, operations());
  CHECK(endpoints);
  const auto control = endpoints->token(EndpointRole::Control), ack = endpoints->token(EndpointRole::Ack);
  endpoints->setAdmissionBeforePublishHookForTest(loseEndpoints, endpoints.get());
  CHECK(!endpoints->bindAdmissionClaim(claim, 7));
  CHECK(claim->contained() && !claim->open());
  CHECK(!endpoints->admissionSnapshot(7, control, ack));
}

struct PublicationRace {
  ChildEndpoints* endpoints;
  std::shared_ptr<RetirementClaim> claim;
  bool rejected = false;
};
void publishDuringContainment(void* context) noexcept
{
  auto* race = static_cast<PublicationRace*>(context);
  race->rejected = !race->endpoints->bindAdmissionClaim(race->claim, 7);
}

void containmentAfterOrdinaryLossRetainsCause()
{
  moduleLive = endpointLive = true;
  auto claim = std::make_shared<RetirementClaim>(7);
  auto module = VerifiedModule::forAccessorTest(7, 42, validModule, nullptr);
  const EndpointInput input[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  auto endpoints = ChildEndpoints::createForTest(module, 7, 100, EndpointMode::Restore, input, operations());
  CHECK(endpoints);
  endpoints->revokeAdmissionForTest();
  PublicationRace race{endpoints.get(), claim};
  endpoints->setAdmissionBeforeContainFlagHookForTest(publishDuringContainment, &race);
  endpoints->invalidateAdmissionForTest();
  CHECK(race.rejected && claim->contained() && !claim->open());
}
}

int main()
{
  snapshotsAreRetainedAndCallbackFree();
  observedModuleLossRevokes();
  observedEndpointLossRevokes();
  callbackLossIsLatched();
  lastCallbacksCannotHideLoss();
  cancellationLossAndEmptyAbort();
  bindingLossDoesNotWaitOrPublish();
  containmentBeforeClaimPublication();
  containmentAfterOrdinaryLossRetainsCause();
}
