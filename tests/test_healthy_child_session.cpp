#include <healthy_restart/child_session.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
using namespace HealthyRetirement;
using namespace HealthyModuleIdentity;
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

// The injected endpoint factory never creates a capability.
namespace HealthyModuleIdentity {
struct VerifiedModule::State {};
VerifiedModule::~VerifiedModule() = default;
bool VerifiedModule::stillValid(std::uint64_t) const noexcept { return false; }
bool VerifiedModule::acceptsEndpoint(std::uintptr_t, std::uint64_t, std::uint64_t) const noexcept { return false; }
}

// This fixture scripts external I/O and graph outcomes; the session, endpoint
// adapter, and WindowsRestartIo under test are the production implementations.
namespace {
struct Pending { void* buffer; DWORD size; bool write; };
struct Script {
  std::map<HANDLE, std::deque<char>> bytes;
  std::map<HANDLE, Pending> pending;
  std::map<HANDLE, bool> eof;
  DWORD error = ERROR_SUCCESS, chunk = 0;
  bool live = true, holdAck = false, revoked = false;
  int grants = 0, controls = 0, loads = 0, seeks = 0, invalidations = 0, writes = 0;
  QByteArray grantFrame;
};
Script* script;
HANDLE handle(int id) { return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(id)); }
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return handle(99); }
BOOL WINAPI closeEvent(HANDLE) { return TRUE; }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return 1; }
DWORD WINAPI lastError() { return script->error; }
BOOL WINAPI cancelIo(HANDLE, LPOVERLAPPED) { script->error = ERROR_NOT_FOUND; return FALSE; }
BOOL transfer(HANDLE pipe, void* buffer, DWORD size, LPDWORD count)
{
  auto& bytes = script->bytes[pipe];
  if (bytes.empty()) {
    script->error = script->eof[pipe] ? ERROR_BROKEN_PIPE : ERROR_IO_PENDING;
    return FALSE;
  }
  const auto n = std::min<std::size_t>(bytes.size(), script->chunk ? std::min(size, script->chunk) : size);
  for (std::size_t i = 0; i < n; ++i) { static_cast<char*>(buffer)[i] = bytes.front(); bytes.pop_front(); }
  *count = static_cast<DWORD>(n);
  return TRUE;
}
BOOL WINAPI readIo(HANDLE h, LPVOID b, DWORD n, LPDWORD count, LPOVERLAPPED)
{
  if (transfer(h, b, n, count)) return TRUE;
  if (script->error == ERROR_IO_PENDING) script->pending[h] = {b, n, false};
  return FALSE;
}
BOOL WINAPI writeIo(HANDLE h, LPCVOID b, DWORD n, LPDWORD count, LPOVERLAPPED)
{
  ++script->writes;
  if (script->holdAck) { script->pending[h] = {const_cast<void*>(b), n, true}; script->error = ERROR_IO_PENDING; return FALSE; }
  *count = n; return TRUE;
}
BOOL WINAPI result(HANDLE h, LPOVERLAPPED, LPDWORD count, BOOL)
{
  auto it = script->pending.find(h);
  if (it == script->pending.end()) { script->error = ERROR_IO_INCOMPLETE; return FALSE; }
  const auto op = it->second;
  if (op.write) {
    if (script->holdAck) { script->error = ERROR_IO_INCOMPLETE; return FALSE; }
    *count = op.size; script->pending.erase(it); return TRUE;
  }
  if (!transfer(h, op.buffer, op.size, count)) {
    if (script->error == ERROR_IO_PENDING) script->error = ERROR_IO_INCOMPLETE;
    else script->pending.erase(it);
    return FALSE;
  }
  script->pending.erase(it); return TRUE;
}
void feed(int id, const QByteArray& data)
{ for (const auto c : data) script->bytes[handle(id)].push_back(c); }

std::shared_ptr<ChildEndpoints> endpoints()
{
  ChildEndpoints::TestOperations o;
  o.valid = [] { return script->live; };
  o.accepts = [](std::uintptr_t v) { return v != 0; };
  o.compare = [](std::uintptr_t a, std::uintptr_t b) { return a == b ? EndpointComparison::Same : EndpointComparison::Different; };
  o.duplicate = [](std::uintptr_t v, HANDLE& out) { out = handle(static_cast<int>(v + 100)); return true; };
  o.close = [](HANDLE) {};
  o.io.createEvent = event; o.io.closeHandle = closeEvent; o.io.resetEvent = resetEvent;
  o.io.readFile = readIo; o.io.writeFile = writeIo; o.io.result = result;
  o.io.error = lastError; o.io.cancel = cancelIo; o.io.now = now;
  const EndpointInput inputs[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  return ChildEndpoints::createForTest(100, EndpointMode::Restore, inputs, o);
}
ChildSession::TestOperations operations()
{
  ChildSession::TestOperations o;
  o.valid = [] { return script->live; };
  o.initialize = [](const QString&) { return true; };
  o.grant = [](const QByteArray& frame, bool eof, QByteArray& ack) {
    ++script->grants; script->grantFrame = frame; ack = "grant-ack"; return eof && !script->revoked;
  };
  o.control = [](const QByteArray&, QByteArray& ack) {
    ++script->controls; script->revoked = true; ack = "control-ack"; return true;
  };
  o.load = [] { ++script->loads; return !script->revoked; };
  o.seek = [] { ++script->seeks; return !script->revoked; };
  o.revoke = [] { script->revoked = true; return true; };
  o.invalidate = [] { ++script->invalidations; };
  return o;
}
QByteArray grant() { return QByteArray::fromHex("00000003616263"); }
QByteArray cancel() { return QByteArray("JRC1") + QByteArray(20, '\0'); }
void poll(ChildSession& session, int count = 8) { while (count--) session.pollRestore(); }

void validGrantAndPartialReads()
{
  Script s; script = &s; s.chunk = 1; feed(101, grant()); s.eof[handle(101)] = true;
  auto e = endpoints(); CHECK(e); ChildSession session(e, 1, operations());
  CHECK(session.beginRestore(QStringLiteral("fixture"))); poll(session);
  CHECK(s.grants == 1 && s.grantFrame == grant() && s.writes == 1);
  CHECK(session.pollRestore() == ChildSession::Progress::Ready);
}
void invalidGrant(const QByteArray& data, bool eof)
{
  Script s; script = &s; feed(101, data); s.eof[handle(101)] = eof;
  auto e = endpoints(); CHECK(e); ChildSession session(e, 1, operations());
  session.beginRestore(QStringLiteral("fixture")); poll(session);
  CHECK(s.grants == 0 && s.loads == 0 && s.seeks == 0);
  if (eof) CHECK(s.invalidations == 1);
}
void cancellationBeforeGrant()
{
  Script s; script = &s; feed(101, grant()); feed(102, cancel()); s.eof[handle(101)] = true;
  auto e = endpoints(); CHECK(e); ChildSession session(e, 1, operations());
  CHECK(session.beginRestore(QStringLiteral("fixture"))); poll(session);
  CHECK(s.controls == 1 && s.revoked && !session.load() && !session.seek());
  CHECK(s.loads == 0 && s.seeks == 0);
}
void pendingAckAndCapabilityLoss()
{
  Script s; script = &s; s.holdAck = true; feed(101, grant()); s.eof[handle(101)] = true;
  auto e = endpoints(); CHECK(e); ChildSession session(e, 1, operations());
  CHECK(session.beginRestore(QStringLiteral("fixture"))); poll(session);
  CHECK(s.grants == 1 && s.writes == 1 && !session.load() && !session.seek());
  s.live = false; CHECK(session.pollRestore() == ChildSession::Progress::Failed);
  CHECK(s.invalidations == 1 && s.loads == 0 && s.seeks == 0);
}
void controlAckSerializesFurtherDispatch()
{
  Script s; script = &s; feed(101, grant()); s.eof[handle(101)] = true;
  auto e = endpoints(); CHECK(e); ChildSession session(e, 1, operations());
  CHECK(session.beginRestore(QStringLiteral("fixture"))); poll(session);
  CHECK(session.pollRestore() == ChildSession::Progress::Ready);
  s.holdAck = true; feed(102, cancel()); poll(session);
  CHECK(s.controls == 1 && s.writes == 2 && s.revoked);
  CHECK(!session.load() && !session.seek() && s.loads == 0 && s.seeks == 0);
  s.holdAck = false; poll(session);
  CHECK(s.writes == 2 && !session.load() && !session.seek());
}
}
int main()
{
  validGrantAndPartialReads();
  invalidGrant(QByteArray::fromHex("00002400"), true);
  invalidGrant(QByteArray::fromHex("0000000361"), true);
  invalidGrant(grant() + QByteArray("x"), true);
  cancellationBeforeGrant();
  pendingAckAndCapabilityLoss();
  controlAckSerializesFurtherDispatch();
}
