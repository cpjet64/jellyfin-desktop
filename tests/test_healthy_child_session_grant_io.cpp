#include <healthy_restart/module_endpoints.h>
#include <QByteArray>
#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <map>
using namespace HealthyModuleIdentity;
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); std::abort(); } } while (false)
namespace HealthyModuleIdentity {
struct VerifiedModule::State {};
VerifiedModule::~VerifiedModule() = default;
bool VerifiedModule::stillValid(std::uint64_t) const noexcept { return false; }
bool VerifiedModule::acceptsEndpoint(std::uintptr_t, std::uint64_t, std::uint64_t) const noexcept { return false; }
}

namespace {
struct Pending { void* buffer; DWORD size; };
struct Script { std::map<HANDLE, std::deque<char>> bytes; std::map<HANDLE, Pending> pending; DWORD error = 0; bool live = true; };
Script* script;
ChildEndpoints* reentrantEndpoint = nullptr;
bool reenterPoll = false, nestedConsumed = false;
HANDLE handle(int id) { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(id)); }
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return handle(99); }
BOOL WINAPI closeEvent(HANDLE) { return TRUE; }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return 1; }
DWORD WINAPI lastError() { return script->error; }
BOOL WINAPI writeIo(HANDLE, LPCVOID, DWORD size, LPDWORD count, LPOVERLAPPED) { *count = size; return TRUE; }
BOOL transfer(HANDLE h, void* buffer, DWORD size, LPDWORD count)
{
  auto& data = script->bytes[h];
  if (data.empty()) { script->error = ERROR_IO_PENDING; return FALSE; }
  const auto n = std::min<std::size_t>(data.size(), size);
  for (std::size_t i = 0; i < n; ++i) { static_cast<char*>(buffer)[i] = data.front(); data.pop_front(); }
  *count = static_cast<DWORD>(n); return TRUE;
}
BOOL WINAPI readIo(HANDLE h, LPVOID buffer, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (transfer(h, buffer, size, count)) return TRUE;
  script->pending[h] = {buffer, size}; return FALSE;
}
BOOL WINAPI result(HANDLE h, LPOVERLAPPED, LPDWORD count, BOOL)
{
  if (reenterPoll) {
    reenterPoll = false;
    const auto id = reentrantEndpoint->grantReadOperation();
    CHECK(reentrantEndpoint->poll(EndpointRole::Grant) == IoProgress::Complete);
    const auto settled = reentrantEndpoint->grantReadSettlement();
    CHECK(settled && settled->operation == id && settled->transferred == 1 && settled->bytes[0] == 'r');
    nestedConsumed = reentrantEndpoint->consumeGrantReadSettlement(id);
    script->error = ERROR_IO_INCOMPLETE; return FALSE;
  }
  auto it = script->pending.find(h);
  if (it == script->pending.end() || !transfer(h, it->second.buffer, it->second.size, count)) {
    script->error = ERROR_IO_INCOMPLETE; return FALSE;
  }
  script->pending.erase(it); return TRUE;
}
void feed(int id, const QByteArray& data)
{ for (const auto c : data) script->bytes[handle(id)].push_back(c); }
int closedHandles = 0;
BOOL WINAPI requestCancel(HANDLE, LPOVERLAPPED) { return TRUE; }
std::shared_ptr<ChildEndpoints> trackedEndpoints(EndpointMode mode = EndpointMode::Restore)
{
  ChildEndpoints::TestOperations o;
  o.valid = [] { return script->live; };
  o.accepts = [](std::uintptr_t value) { return value != 0; };
  o.compare = [](std::uintptr_t a, std::uintptr_t b) { return a == b ? EndpointComparison::Same : EndpointComparison::Different; };
  o.duplicate = [](std::uintptr_t value, HANDLE& out) { out = handle(static_cast<int>(value + 100)); return true; };
  o.close = [](HANDLE) { ++closedHandles; return true; };
  o.io.createEvent = event; o.io.closeHandle = closeEvent; o.io.resetEvent = resetEvent;
  o.io.readFile = readIo; o.io.writeFile = writeIo; o.io.result = result;
  o.io.error = lastError; o.io.cancel = requestCancel; o.io.now = now;
  const EndpointInput restore[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  const EndpointInput source[] = {{EndpointRole::Evidence, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  return mode == EndpointMode::Restore ? ChildEndpoints::createForTest(100, mode, restore, o) :
      ChildEndpoints::createForTest(100, mode, source, o);
}
void latePartialCompletion()
{
  Script s; script = &s; closedHandles = 0;
  auto p = trackedEndpoints(); CHECK(p);
  CHECK(p->beginTrackedGrantRead(8) == IoProgress::Pending);
  const auto operation = p->grantReadOperation(); CHECK(operation != 0);
  CHECK(!p->grantReadSettlement() && !p->bytes(EndpointRole::Grant));
  CHECK(p->beginTrackedGrantRead(8) == IoProgress::Failed);
  CHECK(p->begin(EndpointRole::Grant, true, nullptr, 8) == IoProgress::Failed);
  CHECK(p->cancel(EndpointRole::Grant) == IoProgress::Unconfirmed);
  CHECK(p->poll(EndpointRole::Grant) == IoProgress::Pending);
  feed(101, QByteArray("abc"));
  CHECK(p->poll(EndpointRole::Grant) == IoProgress::Complete);
  const auto settled = p->grantReadSettlement(); CHECK(settled);
  CHECK(settled->operation == operation && settled->outcome == WindowsRestartIo::Settlement::Outcome::Completed);
  CHECK(settled->transferred == 3 && QByteArray(reinterpret_cast<const char*>(settled->bytes.data()), 3) == "abc");
  CHECK(!p->bytes(EndpointRole::Grant) && !p->consumeGrantReadSettlement(operation + 1));
  CHECK(p->beginTrackedGrantRead(2) == IoProgress::Failed);
  CHECK(p->consumeGrantReadSettlement(operation) && !p->consumeGrantReadSettlement(operation));
  CHECK(!p->grantReadSettlement() && settled->transferred == 3 && settled->bytes[0] == 'a');
  CHECK(p->beginTrackedGrantRead(2) == IoProgress::Pending);
}
void invalidatedCapabilityStillSettles()
{
  Script s; script = &s; closedHandles = 0;
  auto p = trackedEndpoints(); CHECK(p);
  CHECK(p->beginTrackedGrantRead(4) == IoProgress::Pending);
  const auto operation = p->grantReadOperation(); s.live = false;
  CHECK(!p->token(EndpointRole::Grant));
  CHECK(p->cancel(EndpointRole::Grant) == IoProgress::Unconfirmed);
  feed(101, QByteArray("z")); CHECK(p->poll(EndpointRole::Grant) == IoProgress::Complete);
  const auto settled = p->grantReadSettlement(); CHECK(settled && settled->transferred == 1 && settled->bytes[0] == 'z');
  CHECK(p->consumeGrantReadSettlement(operation));
  CHECK(p->beginTrackedGrantRead(1) == IoProgress::Failed);
  CHECK(p->begin(EndpointRole::Grant, true, nullptr, 1) == IoProgress::Failed);
}
void retainedOwnershipAndRole()
{
  Script s; script = &s; closedHandles = 0;
  auto p = trackedEndpoints(EndpointMode::Source); CHECK(p);
  CHECK(p->beginTrackedGrantRead(1) == IoProgress::Failed);
  p.reset(); CHECK(closedHandles == 3);
  closedHandles = 0; p = trackedEndpoints(); CHECK(p);
  CHECK(p->beginTrackedGrantRead(3) == IoProgress::Pending);
  p.reset(); CHECK(closedHandles == 0);
  closedHandles = 0; p = trackedEndpoints(); CHECK(p); feed(101, QByteArray("q"));
  CHECK(p->beginTrackedGrantRead(3) == IoProgress::Complete);
  CHECK(p->grantReadSettlement()); p.reset(); CHECK(closedHandles == 0);
}
void reentrantConsumption()
{
  Script s; script = &s; closedHandles = 0; nestedConsumed = false;
  auto p = trackedEndpoints(); CHECK(p);
  CHECK(p->beginTrackedGrantRead(4) == IoProgress::Pending);
  feed(101, QByteArray("r")); reentrantEndpoint = p.get(); reenterPoll = true;
  CHECK(p->poll(EndpointRole::Grant) == IoProgress::Complete);
  CHECK(nestedConsumed && !p->grantReadSettlement() && !p->bytes(EndpointRole::Grant));
  reentrantEndpoint = nullptr;
}
}
int main() { latePartialCompletion(); invalidatedCapabilityStillSettles(); retainedOwnershipAndRole(); reentrantConsumption(); }
