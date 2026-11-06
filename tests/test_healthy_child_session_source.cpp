#define main childRestoreTests
#include "test_healthy_child_session.cpp"
#undef main
#include <fixture_restart/RestartWire.h>

// Reuse the injected OS harness while driving the real source-session pump.
namespace {
int cancellationPolls = 0;
bool abortCancellation = false, completeCancellationWithData = false;
std::vector<int> closedRoles;
std::vector<QByteArray> writtenFrames;
BOOL WINAPI sourceWrite(HANDLE h, LPCVOID data, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  if (script->holdAck) return writeIo(h, data, size, count, overlap);
  writtenFrames.emplace_back(static_cast<const char*>(data), static_cast<qsizetype>(size));
  return writeIo(h, data, size, count, overlap);
}
BOOL WINAPI sourceCancel(HANDLE, LPOVERLAPPED)
{ abortCancellation = true; return TRUE; }
BOOL WINAPI sourceResult(HANDLE h, LPOVERLAPPED overlap, LPDWORD count, BOOL wait)
{
  if (abortCancellation && h == handle(102)) {
    if (cancellationPolls-- > 0) { script->error = ERROR_IO_INCOMPLETE; return FALSE; }
    if (completeCancellationWithData) {
      auto it = script->pending.find(h);
      if (it != script->pending.end()) {
        std::memset(it->second.buffer, 1, it->second.size);
        *count = it->second.size; script->pending.erase(it); return TRUE;
      }
    }
    *count = 0; script->error = ERROR_OPERATION_ABORTED; return FALSE;
  }
  return result(h, overlap, count, wait);
}
std::shared_ptr<ChildEndpoints> sourceEndpoints()
{
  ChildEndpoints::TestOperations o;
  o.valid = [] { return script->live; };
  o.accepts = [](std::uintptr_t value) { return value != 0; };
  o.compare = [](std::uintptr_t a, std::uintptr_t b) { return a == b ? EndpointComparison::Same : EndpointComparison::Different; };
  o.duplicate = [](std::uintptr_t value, HANDLE& out) { out = handle(static_cast<int>(value + 100)); return true; };
  o.close = [](HANDLE h) { closedRoles.push_back(static_cast<int>(reinterpret_cast<std::uintptr_t>(h)) - 100); return true; };
  o.io.createEvent = event; o.io.closeHandle = closeEvent; o.io.resetEvent = resetEvent;
  o.io.readFile = readIo; o.io.writeFile = sourceWrite; o.io.result = sourceResult;
  o.io.error = lastError; o.io.cancel = sourceCancel; o.io.now = now;
  const EndpointInput inputs[] = {{EndpointRole::Evidence, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  return ChildEndpoints::createForTest(100, EndpointMode::Source, inputs, o);
}
ChildSession::TestOperations sourceOperations()
{
  auto o = operations();
  o.sourceLoad = [](const QString&, bool, std::uint64_t& run, std::uint64_t& launch, std::uint64_t& revision) {
    run = 10; launch = 20; revision = 30; return true;
  };
  o.sourceFreeze = [](QByteArray& frame) { frame = "evidence"; return true; };
  o.sourceBeginStop = [](const QByteArray&) { return true; };
  o.sourcePollStop = [](QByteArray& ack) { ack = "stop-ack"; return ChildSession::Progress::Ready; };
  o.sourceRetire = [] { return true; };
  o.sourcePollRetire = [] { return ChildSession::Progress::Ready; };
  o.sourceClosed = [](ChildSession::SourceClosed& closed) {
    closed = {40, 50, 60, 30}; return true;
  };
  return o;
}
void resetSource()
{ cancellationPolls = 0; abortCancellation = false; completeCancellationWithData = false; closedRoles.clear(); writtenFrames.clear(); }
void beginSource(ChildSession& session)
{
  CHECK(session.beginSource(QStringLiteral("fixture"), true));
  CHECK(session.freezeSource());
  feed(102, FixtureRestart::encodeControl({FixtureRestart::ControlKind::InternalStop, 70, 30}));
}
void completedCancellation()
{
  Script s; script = &s; resetSource(); cancellationPolls = 2;
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, sourceOperations());
  beginSource(session);
  for (int i = 0; i < 4; ++i) CHECK(session.pollSource() == ChildSession::Progress::Pending);
  for (int i = 0; i < 8 && session.pollSource() != ChildSession::Progress::Ready; ++i) {}
  CHECK(session.pollSource() == ChildSession::Progress::Ready);
  CHECK(closedRoles.size() == 2 && closedRoles[0] == 1 && closedRoles[1] == 3);
  CHECK(writtenFrames.size() == 3 && writtenFrames[0] == "evidence" && writtenFrames[1] == "stop-ack");
  CHECK(writtenFrames[2] == FixtureRestart::encodeClosed({10, 20, 70, 30, 50, 40, 60}));
}
void dataRacingCancellation()
{
  Script s; script = &s; resetSource(); completeCancellationWithData = true;
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, sourceOperations());
  beginSource(session);
  for (int i = 0; i < 8 && session.pollSource() != ChildSession::Progress::Failed; ++i) {}
  CHECK(session.pollSource() == ChildSession::Progress::Failed);
  CHECK(writtenFrames.size() == 2 && closedRoles.size() == 1 && closedRoles[0] == 1);
}
void extraControl(const QByteArray& extra)
{
  Script s; script = &s; resetSource();
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, sourceOperations());
  beginSource(session); feed(102, extra);
  for (int i = 0; i < 8 && session.pollSource() != ChildSession::Progress::Failed; ++i) {}
  CHECK(session.pollSource() == ChildSession::Progress::Failed);
  CHECK(writtenFrames.size() == (extra.size() < 24 ? 2 : 1));
  CHECK(closedRoles.size() == 1 && closedRoles[0] == 1);
}
void authorityLossDuringCancellation()
{
  Script s; script = &s; resetSource(); cancellationPolls = 10;
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, sourceOperations());
  beginSource(session);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  s.live = false;
  CHECK(session.pollSource() == ChildSession::Progress::Failed);
  CHECK(writtenFrames.size() == 2 && closedRoles.size() == 1 && closedRoles[0] == 1);
}
void pendingEvidenceWrite()
{
  Script s; script = &s; resetSource(); s.holdAck = true;
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, sourceOperations());
  beginSource(session);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  CHECK(closedRoles.empty() && s.pending.count(handle(101)) == 1);
  s.holdAck = false;
  for (int i = 0; i < 8 && session.pollSource() != ChildSession::Progress::Ready; ++i) {}
  CHECK(session.pollSource() == ChildSession::Progress::Ready);
  CHECK(closedRoles.size() == 2 && closedRoles[0] == 1 && closedRoles[1] == 3);
}
void pendingStopAck()
{
  Script s; script = &s; resetSource();
  auto ops = sourceOperations();
  ops.sourcePollStop = [](QByteArray& ack) {
    ack = "stop-ack"; script->holdAck = true; return ChildSession::Progress::Ready;
  };
  auto e = sourceEndpoints(); CHECK(e); ChildSession session(e, 40, ops);
  beginSource(session);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  CHECK(session.pollSource() == ChildSession::Progress::Pending);
  CHECK(closedRoles.size() == 1 && closedRoles[0] == 1 && s.pending.count(handle(103)) == 1);
  s.holdAck = false;
  for (int i = 0; i < 8 && session.pollSource() != ChildSession::Progress::Ready; ++i) {}
  CHECK(session.pollSource() == ChildSession::Progress::Ready);
  CHECK(closedRoles.size() == 2 && closedRoles[1] == 3);
}
}
int main()
{
  completedCancellation(); dataRacingCancellation(); extraControl(QByteArray("x"));
  extraControl(cancel()); authorityLossDuringCancellation(); pendingEvidenceWrite(); pendingStopAck();
}
