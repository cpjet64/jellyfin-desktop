#include "fixture_restart/WindowsRestartIo.h"
#include <cstdlib>
#include <cstring>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
DWORD errorCode = ERROR_IO_PENDING;
ULONGLONG clockValue = 1;
bool complete = false, cancelCalled = false;
bool synchronousLate = false;
int readMode = 0;
bool resultEof = false;
DWORD completedBytes = 0;
HANDLE WINAPI eventCreate(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR)
{ return reinterpret_cast<HANDLE>(1); }
BOOL WINAPI closeFake(HANDLE) { return TRUE; }
BOOL WINAPI resetFake(HANDLE) { return TRUE; }
DWORD WINAPI errorFake() { return errorCode; }
ULONGLONG WINAPI clockFake() { return clockValue; }
BOOL WINAPI writeFake(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED)
{ errorCode = ERROR_IO_PENDING; return FALSE; }
BOOL WINAPI readFake(HANDLE, LPVOID, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (readMode == 1) { errorCode = ERROR_BROKEN_PIPE; return FALSE; }
  if (readMode == 2) { readMode = 1; *count = 1; return TRUE; }
  if (synchronousLate) { clockValue = 5; *count = size; return TRUE; }
  errorCode = ERROR_IO_PENDING; return FALSE;
}
BOOL WINAPI resultFake(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{
  if (resultEof) { errorCode = ERROR_NO_DATA; return FALSE; }
  if (!complete) { errorCode = ERROR_IO_INCOMPLETE; return FALSE; }
  *count = completedBytes;
  return TRUE;
}
BOOL WINAPI cancelFake(HANDLE, LPOVERLAPPED)
{ cancelCalled = true; return TRUE; }
WindowsIoApi fakeApi()
{
  WindowsIoApi api;
  api.createEvent = eventCreate; api.closeHandle = closeFake;
  api.resetEvent = resetFake; api.readFile = readFake; api.writeFile = writeFake;
  api.result = resultFake; api.cancel = cancelFake;
  api.error = errorFake; api.now = clockFake;
  return api;
}
void cancellationRetainsOwnership()
{
  auto* io = new WindowsRestartIo(fakeApi());
  const char payload[] = "abc";
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), false, payload, 3, 10) == IoProgress::Pending);
  CHECK(io->cancel() == IoProgress::Unconfirmed && cancelCalled);
  CHECK(!io->release());
  complete = true; completedBytes = 3;
  CHECK(io->poll() == IoProgress::Failed);
  CHECK(io->release());
}
void deadlineCancelsAndRetains()
{
  complete = false; cancelCalled = false; clockValue = 1;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 4, 5) == IoProgress::Pending);
  clockValue = 5;
  CHECK(io->poll() == IoProgress::Unconfirmed && cancelCalled);
  CHECK(!io->release());
  complete = true; completedBytes = 4;
  CHECK(io->poll() == IoProgress::Failed && io->release());
}
void completesWithoutCancellation()
{
  complete = false; clockValue = 1;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 4, 5) == IoProgress::Pending);
  complete = true; completedBytes = 4;
  CHECK(io->poll() == IoProgress::Complete && io->release());
}
void lateCompletionFails()
{
  complete = false; clockValue = 1;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 4, 5) == IoProgress::Pending);
  complete = true; completedBytes = 4; clockValue = 5;
  CHECK(io->poll() == IoProgress::Failed && io->release());
  clockValue = 1; synchronousLate = true;
  io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 4, 5) == IoProgress::Failed);
  CHECK(io->release());
}
void eofIsDistinctAndPartialFails()
{
  synchronousLate = false; resultEof = false; clockValue = 1; readMode = 1;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 1, 5) == IoProgress::Eof);
  CHECK(io->release());
  readMode = 2; io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 4, 5) == IoProgress::Failed);
  CHECK(io->release());
  readMode = 0; complete = false; io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 1, 5) == IoProgress::Pending);
  resultEof = true;
  CHECK(io->poll() == IoProgress::Eof && io->release());
  resultEof = false;
}
void extraByteAndCancelledEof()
{
  clockValue = 1; complete = false; readMode = 0;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 1, 5) == IoProgress::Pending);
  complete = true; completedBytes = 1;
  CHECK(io->poll() == IoProgress::Complete && io->release());
  complete = false; io = new WindowsRestartIo(fakeApi());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 1, 5) == IoProgress::Pending);
  resultEof = true;
  CHECK(io->cancel() == IoProgress::Failed && io->release());
  resultEof = false;
}
}
int main()
{
  cancellationRetainsOwnership();
  deadlineCancelsAndRetains();
  completesWithoutCancellation();
  lateCompletionFails();
  eofIsDistinctAndPartialFails();
  extraByteAndCancelledEof();
}
