#include <healthy_restart/module_endpoints.h>
#include <cstdlib>
using namespace HealthyModuleIdentity;
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

// The production capability is never created by this isolated test.
namespace HealthyModuleIdentity {
struct VerifiedModule::State {};
VerifiedModule::~VerifiedModule() = default;
bool VerifiedModule::stillValid(std::uint64_t) const noexcept { return false; }
bool VerifiedModule::acceptsEndpoint(std::uintptr_t, std::uint64_t, std::uint64_t) const noexcept { return false; }
}
namespace {
int duplicated = 0, closed = 0, writes = 0, reads = 0;
bool live = true; int failDuplicate = 0;
int readMode = 0; bool canceled = false;
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(99); }
BOOL WINAPI closeEvent(HANDLE) { return TRUE; }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return 1; }
BOOL WINAPI writeIo(HANDLE, LPCVOID, DWORD size, LPDWORD count, LPOVERLAPPED)
{ ++writes; *count = size; return TRUE; }
BOOL WINAPI readIo(HANDLE, LPVOID, DWORD size, LPDWORD count, LPOVERLAPPED)
{ ++reads; if (readMode) return FALSE; *count = size; return TRUE; }
DWORD WINAPI error() { return canceled ? ERROR_OPERATION_ABORTED : (readMode == 1 ? ERROR_BROKEN_PIPE : ERROR_IO_PENDING); }
BOOL WINAPI cancel(HANDLE, LPOVERLAPPED) { canceled = true; return TRUE; }
BOOL WINAPI result(HANDLE, LPOVERLAPPED, LPDWORD, BOOL) { return FALSE; }
ChildEndpoints::TestOperations operations()
{
  ChildEndpoints::TestOperations o;
  o.valid = [] { return live; };
  o.accepts = [](std::uintptr_t v) { return v != 0; };
  o.compare = [](std::uintptr_t a, std::uintptr_t b) { return a == b ? EndpointComparison::Same : EndpointComparison::Different; };
  o.duplicate = [](std::uintptr_t v, HANDLE& out) {
    if (++duplicated == failDuplicate) return false;
    out = reinterpret_cast<HANDLE>(v + 100); return true;
  };
  o.close = [](HANDLE) { ++closed; };
  o.io.createEvent = event; o.io.closeHandle = closeEvent; o.io.resetEvent = resetEvent;
  o.io.writeFile = writeIo; o.io.readFile = readIo; o.io.now = now;
  o.io.error = error; o.io.cancel = cancel; o.io.result = result;
  return o;
}
void cases()
{
  const EndpointInput source[] = {{EndpointRole::Evidence, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  auto p = ChildEndpoints::createForTest(100, EndpointMode::Source, source, operations());
  CHECK(p && !p->capability() && p->token(EndpointRole::Evidence) && !p->token(EndpointRole::Grant));
  CHECK(!p->bytes(EndpointRole::Control));
  char data = 'x';
  CHECK(p->begin(EndpointRole::Evidence, true, &data, 1) == IoProgress::Failed);
  CHECK(p->begin(EndpointRole::Evidence, false, &data, 1) == IoProgress::Complete && writes == 1);
  CHECK(!p->bytes(EndpointRole::Evidence));
  CHECK(p->begin(EndpointRole::Control, false, &data, 1) == IoProgress::Failed);
  CHECK(p->begin(EndpointRole::Control, true, &data, 1) == IoProgress::Complete && reads == 1);
  CHECK(p->bytes(EndpointRole::Control) && p->bytes(EndpointRole::Control)->size() == 1);
  live = false; CHECK(!p->token(EndpointRole::Ack));
  CHECK(p->begin(EndpointRole::Ack, false, &data, 1) == IoProgress::Failed);
  p.reset(); CHECK(closed == 3); live = true;
  EndpointInput duplicate[] = {{EndpointRole::Evidence, 1}, {EndpointRole::Control, 1}, {EndpointRole::Ack, 3}};
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, duplicate, operations()));
  duplicate[1] = {EndpointRole::Evidence, 2};
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, duplicate, operations()));
  live = false; CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, source, operations())); live = true;
  CHECK(!ChildEndpoints::createForTest(100, static_cast<EndpointMode>(99), source, operations()));
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Restore, source, operations()));
  auto errorOps = operations(); errorOps.compare = [](std::uintptr_t, std::uintptr_t) { return EndpointComparison::Error; };
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, source, errorOps));
  duplicated = closed = 0;
  auto aliasOps = operations();
  aliasOps.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a >= 100 && b >= 100 ? EndpointComparison::Same : EndpointComparison::Different;
  };
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, source, aliasOps));
  CHECK(duplicated == 2 && closed == 2);
  duplicated = closed = 0;
  auto copiedError = operations();
  copiedError.compare = [](std::uintptr_t a, std::uintptr_t b) {
    return a >= 100 && b >= 100 ? EndpointComparison::Error : EndpointComparison::Different;
  };
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, source, copiedError));
  CHECK(duplicated == 2 && closed == 2);
  duplicated = closed = 0; failDuplicate = 3;
  CHECK(!ChildEndpoints::createForTest(100, EndpointMode::Source, source, operations()));
  CHECK(closed == 2); failDuplicate = 0;
  const EndpointInput restore[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  p = ChildEndpoints::createForTest(100, EndpointMode::Restore, restore, operations()); CHECK(p);
  CHECK(p->begin(EndpointRole::Grant, false, &data, 1) == IoProgress::Failed);
  CHECK(p->begin(EndpointRole::Grant, true, &data, 1) == IoProgress::Complete);
  p.reset(); readMode = 1;
  p = ChildEndpoints::createForTest(100, EndpointMode::Restore, restore, operations()); CHECK(p);
  CHECK(p->begin(EndpointRole::Grant, true, &data, 1) != IoProgress::Complete && !p->bytes(EndpointRole::Grant));
  p.reset(); readMode = 2;
  p = ChildEndpoints::createForTest(100, EndpointMode::Restore, restore, operations()); CHECK(p);
  CHECK(p->begin(EndpointRole::Grant, true, &data, 1) == IoProgress::Pending && !p->bytes(EndpointRole::Grant));
  CHECK(p->cancel(EndpointRole::Grant) != IoProgress::Complete && !p->bytes(EndpointRole::Grant));
}
}
int main() { cases(); }
