#define main childRestoreTests
#include "test_healthy_child_session.cpp"
#undef main

namespace {
bool cancelRequested = false, abortGrant = false;
std::vector<QByteArray> ackFrames;
BOOL WINAPI cancelRead(HANDLE, LPOVERLAPPED) { cancelRequested = true; return TRUE; }
BOOL WINAPI cancelResult(HANDLE h, LPOVERLAPPED overlap, LPDWORD count, BOOL wait)
{
  if (h == handle(101) && cancelRequested && abortGrant) {
    *count = 0; script->error = ERROR_OPERATION_ABORTED; return FALSE;
  }
  return result(h, overlap, count, wait);
}
BOOL WINAPI captureWrite(HANDLE h, LPCVOID data, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  ackFrames.emplace_back(static_cast<const char*>(data), static_cast<qsizetype>(size));
  return writeIo(h, data, size, count, overlap);
}
std::shared_ptr<ChildEndpoints> cancellationEndpoints()
{
  ChildEndpoints::TestOperations o;
  o.valid = [] { return script->live; };
  o.accepts = [](std::uintptr_t v) { return v != 0; };
  o.compare = [](std::uintptr_t a, std::uintptr_t b) { return a == b ? EndpointComparison::Same : EndpointComparison::Different; };
  o.duplicate = [](std::uintptr_t v, HANDLE& out) { out = handle(static_cast<int>(v + 100)); return true; };
  o.close = [](HANDLE) { return true; };
  o.io.createEvent = event; o.io.closeHandle = closeEvent; o.io.resetEvent = resetEvent;
  o.io.readFile = readIo; o.io.writeFile = captureWrite; o.io.result = cancelResult;
  o.io.error = lastError; o.io.cancel = cancelRead; o.io.now = now;
  const EndpointInput input[] = {{EndpointRole::Grant, 1}, {EndpointRole::Control, 2}, {EndpointRole::Ack, 3}};
  return ChildEndpoints::createForTest(100, EndpointMode::Restore, input, o);
}
void cancellationAfterPending(bool abort)
{
  Script s; script = &s; cancelRequested = abortGrant = false; ackFrames.clear();
  auto e = cancellationEndpoints(); CHECK(e);
  auto ops = operations(); int validations = 0; ChildSession* current = nullptr;
  ops.control = [](const QByteArray&, QByteArray& ack) { ack = "no-arm"; return true; };
  ops.validateCanceledGrant = [&](const QByteArray&) { ++validations; return false; };
  ops.pollCancellation = [&](QByteArray& ack) { if (current->grantReadSettled()) ack = "terminal"; return true; };
  ChildSession session(e, 1, ops); current = &session;
  CHECK(session.beginRestore(QStringLiteral("fixture"))); feed(102, cancel());
  for (int i = 0; i < 4; ++i) CHECK(session.pollRestore() == ChildSession::Progress::Pending);
  CHECK(cancelRequested && ackFrames.size() == 1 && ackFrames[0] == "no-arm");
  CHECK(!session.grantReadSettled() && s.grants == 0 && s.loads == 0 && s.seeks == 0);
  if (abort) abortGrant = true;
  else feed(101, QByteArray("ab"));
  for (int i = 0; i < 5; ++i) session.pollRestore();
  CHECK(session.grantReadSettled());
  CHECK(ackFrames.size() == 1); // NoArm already occupied this callback's terminal response.
  CHECK(validations == 0 && s.grants == 0 && s.loads == 0 && s.seeks == 0);
}
void malformedCompletion()
{
  Script s; script = &s; cancelRequested = abortGrant = false; ackFrames.clear();
  auto e = cancellationEndpoints(); CHECK(e); auto ops = operations(); int validations = 0;
  ops.control = [](const QByteArray&, QByteArray& ack) { ack = "no-arm"; return true; };
  ops.validateCanceledGrant = [&](const QByteArray& frame) { ++validations; CHECK(frame == grant()); return false; };
  ChildSession session(e, 1, ops);
  feed(101, grant().left(4)); CHECK(session.beginRestore(QStringLiteral("fixture")));
  feed(102, cancel()); for (int i = 0; i < 4; ++i) session.pollRestore();
  CHECK(cancelRequested && ackFrames.size() == 1 && ackFrames[0] == "no-arm");
  feed(101, grant().mid(4));
  CHECK(session.pollRestore() == ChildSession::Progress::Failed);
  CHECK(validations == 1 && s.grants == 0 && s.loads == 0 && s.seeks == 0 && ackFrames.size() == 1);
}
void acceptanceThenCancellation()
{
  Script s; script = &s; cancelRequested = abortGrant = false; ackFrames.clear();
  auto e = cancellationEndpoints(); CHECK(e); auto ops = operations();
  // Script the armed terminal type; wire encoding is tested separately.
  ops.control = [](const QByteArray&, QByteArray& ack) { ack = "JCS1-no-load"; return true; };
  feed(101, grant()); s.eof[handle(101)] = true;
  ChildSession session(e, 1, ops); CHECK(session.beginRestore(QStringLiteral("fixture")));
  for (int i = 0; i < 8; ++i) session.pollRestore();
  CHECK(session.pollRestore() == ChildSession::Progress::Ready);
  feed(102, cancel()); for (int i = 0; i < 5; ++i) session.pollRestore();
  CHECK(ackFrames.size() == 2 && ackFrames[0] == "JGA2-ack" && ackFrames[1] == "JCS1-no-load");
  CHECK(!session.load() && !session.seek() && s.loads == 0 && s.seeks == 0);
}
void reentrantNoArm()
{
  Script s; script = &s; cancelRequested = abortGrant = false; ackFrames.clear();
  auto e = cancellationEndpoints(); CHECK(e); auto ops = operations();
  ops.grant = [](const QByteArray&, bool, QByteArray& ack) { ack = "JCN1-terminal"; return true; };
  feed(101, grant()); s.eof[handle(101)] = true;
  ChildSession session(e, 1, ops); CHECK(session.beginRestore(QStringLiteral("fixture")));
  for (int i = 0; i < 8; ++i) CHECK(session.pollRestore() == ChildSession::Progress::Pending);
  CHECK(ackFrames.size() == 1 && ackFrames[0] == "JCN1-terminal");
  CHECK(!session.load() && !session.seek() && s.loads == 0 && s.seeks == 0);
}
}
int main()
{
  cancellationAfterPending(true); cancellationAfterPending(false); malformedCompletion();
  acceptanceThenCancellation(); reentrantNoArm();
}
