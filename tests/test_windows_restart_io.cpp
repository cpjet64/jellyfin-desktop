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
LPVOID pendingBuffer = nullptr;
LPOVERLAPPED pendingOverlap = nullptr;
DWORD resultError = ERROR_IO_INCOMPLETE;
DWORD errorBytes = 0;
bool leaveErrorCountUntouched = false;
DWORD cancelError = ERROR_SUCCESS;
bool reenterCancel = false;
bool reenterRead = false;
bool reenterResult = false;
WindowsRestartIo* reentrantIo = nullptr;
bool readRetained = false;
bool nestedBlocked = false;
int closeCount = 0;
int resultCalls = 0;
HANDLE WINAPI eventCreate(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR)
{ return reinterpret_cast<HANDLE>(1); }
BOOL WINAPI closeFake(HANDLE) { ++closeCount; return TRUE; }
BOOL WINAPI resetFake(HANDLE) { return TRUE; }
DWORD WINAPI errorFake() { return errorCode; }
ULONGLONG WINAPI clockFake() { return clockValue; }
BOOL WINAPI writeFake(HANDLE, LPCVOID, DWORD, LPDWORD, LPOVERLAPPED)
{ errorCode = ERROR_IO_PENDING; return FALSE; }
BOOL WINAPI readFake(HANDLE, LPVOID buffer, DWORD size, LPDWORD count, LPOVERLAPPED overlap)
{
  pendingBuffer = buffer; pendingOverlap = overlap;
  if (reenterRead) {
    reenterRead = false;
    readRetained = !reentrantIo->release() && reentrantIo->pending();
  }
  if (readMode == 1) { errorCode = ERROR_BROKEN_PIPE; return FALSE; }
  if (readMode == 2) { readMode = 1; *count = 1; return TRUE; }
  if (readMode == 3) { *count = 1; errorCode = ERROR_BROKEN_PIPE; return FALSE; }
  if (readMode == 4) { *count = 0; errorCode = ERROR_BROKEN_PIPE; return FALSE; }
  if (synchronousLate) { clockValue = 5; *count = size; return TRUE; }
  errorCode = ERROR_IO_PENDING; return FALSE;
}
BOOL WINAPI resultFake(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{
  ++resultCalls;
  if (reenterResult) {
    reenterResult = false;
    CHECK(reentrantIo->poll() == IoProgress::Complete);
    const auto settled = reentrantIo->settlement();
    CHECK(settled && reentrantIo->consumeSettlement(settled->operation));
    nestedBlocked = !reentrantIo->release() &&
        reentrantIo->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed;
  }
  if (resultEof) { errorCode = ERROR_NO_DATA; return FALSE; }
  if (!complete) {
    if (!leaveErrorCountUntouched) *count = errorBytes;
    errorCode = resultError; return FALSE;
  }
  *count = completedBytes;
  return TRUE;
}
BOOL WINAPI cancelFake(HANDLE, LPOVERLAPPED)
{
  cancelCalled = true;
  if (reenterCancel) { reenterCancel = false; reentrantIo->poll(); }
  if (cancelError != ERROR_SUCCESS) { errorCode = cancelError; return FALSE; }
  return TRUE;
}
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

void resetTracked()
{
  complete = false; cancelCalled = false; synchronousLate = false; readMode = 0;
  resultEof = false; resultError = ERROR_IO_INCOMPLETE; cancelError = ERROR_SUCCESS;
  completedBytes = 0; errorBytes = 0; clockValue = 1; pendingBuffer = nullptr;
  pendingOverlap = nullptr; reenterCancel = false; reentrantIo = nullptr; closeCount = 0;
  resultCalls = 0; reenterRead = false; readRetained = false; leaveErrorCountUntouched = false;
  reenterResult = false; nestedBlocked = false;
}

WindowsRestartIo* trackedRead()
{
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Pending);
  CHECK(pendingBuffer && pendingOverlap && io->pending() && io->operation() != 0);
  return io;
}

void checkCompleted(WindowsRestartIo* io, const char* expected, DWORD count)
{
  const auto settled = io->settlement();
  CHECK(settled && settled->outcome == WindowsRestartIo::Settlement::Outcome::Completed);
  CHECK(settled->operation == io->operation());
  CHECK(settled->transferred == count && settled->bytes.size() == count);
  CHECK(std::memcmp(settled->bytes.data(), expected, count) == 0);
  const auto operation = settled->operation;
  CHECK(!io->release());
  CHECK(!io->consumeSettlement(operation + 1));
  CHECK(io->consumeSettlement(operation));
  CHECK(!io->consumeSettlement(operation));
  CHECK(settled->transferred == count && std::memcmp(settled->bytes.data(), expected, count) == 0);
  CHECK(io->release());
}

void trackedCompletionBeforeCancel()
{
  resetTracked(); auto* io = trackedRead();
  std::memcpy(pendingBuffer, "abcd", 4); complete = true; completedBytes = 4;
  CHECK(io->poll() == IoProgress::Complete);
  CHECK(io->cancel() == IoProgress::Complete && !cancelCalled);
  checkCompleted(io, "abcd", 4);
}

void trackedCancellationThenCompletion(DWORD cancellationError)
{
  resetTracked(); auto* io = trackedRead();
  const auto buffer = pendingBuffer; const auto overlap = pendingOverlap;
  cancelError = cancellationError;
  CHECK(io->cancel() == IoProgress::Unconfirmed && cancelCalled);
  CHECK(io->pending() && !io->settlement() && !io->release() && closeCount == 0);
  CHECK(buffer == pendingBuffer && overlap == pendingOverlap);
  std::memcpy(buffer, "abc", 3); complete = true; completedBytes = 3;
  CHECK(io->poll() == IoProgress::Complete);
  checkCompleted(io, "abc", 3);
}

void trackedAbortAndUnknownExtent()
{
  resetTracked(); auto* io = trackedRead();
  resultError = ERROR_OPERATION_ABORTED;
  CHECK(io->cancel() != IoProgress::Eof);
  const auto aborted = io->settlement();
  CHECK(aborted && aborted->outcome == WindowsRestartIo::Settlement::Outcome::Aborted);
  CHECK(aborted->transferred == 0 && aborted->bytes.empty());
  CHECK(io->consumeSettlement(aborted->operation) && io->release());
  resetTracked(); io = trackedRead(); resultError = ERROR_ACCESS_DENIED;
  CHECK(io->cancel() == IoProgress::Unconfirmed);
  CHECK(io->pending() && !io->release() && closeCount == 0);
  resultError = ERROR_OPERATION_ABORTED;
  CHECK(io->poll() != IoProgress::Eof);
  const auto settled = io->settlement();
  CHECK(settled && settled->outcome != WindowsRestartIo::Settlement::Outcome::Completed);
  CHECK(io->consumeSettlement(settled->operation) && io->release());
  resetTracked(); io = trackedRead(); resultError = ERROR_OPERATION_ABORTED; errorBytes = 1;
  CHECK(io->cancel() == IoProgress::Failed);
  const auto uncertain = io->settlement();
  CHECK(uncertain && uncertain->outcome == WindowsRestartIo::Settlement::Outcome::Uncertain);
  CHECK(io->consumeSettlement(uncertain->operation));
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed);
  CHECK(io->release());
}

void trackedPartialReadsRemainDistinct()
{
  resetTracked(); auto* io = trackedRead();
  std::memcpy(pendingBuffer, "ab", 2); complete = true; completedBytes = 2;
  CHECK(io->poll() == IoProgress::Complete);
  const auto first = io->settlement();
  CHECK(first && first->transferred == 2 && first->bytes[0] == 'a' && first->bytes[1] == 'b');
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed);
  CHECK(io->consumeSettlement(first->operation));
  complete = false;
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Pending);
  std::memcpy(pendingBuffer, "cd", 2); complete = true;
  CHECK(io->poll() == IoProgress::Complete);
  const auto second = io->settlement();
  CHECK(second && second->operation != first->operation && second->transferred == 2);
  CHECK(first->bytes[0] == 'a' && first->bytes[1] == 'b');
  CHECK(second->bytes[0] == 'c' && second->bytes[1] == 'd');
  CHECK(io->consumeSettlement(second->operation) && io->release());
}

void trackedTimeoutAndReentrantCompletion()
{
  resetTracked(); auto* io = trackedRead(); clockValue = 10;
  CHECK(io->poll() == IoProgress::Unconfirmed && !io->release());
  std::memcpy(pendingBuffer, "xy", 2); complete = true; completedBytes = 2;
  CHECK(io->poll() == IoProgress::Complete);
  checkCompleted(io, "xy", 2);
  resetTracked(); io = trackedRead(); reentrantIo = io;
  std::memcpy(pendingBuffer, "z", 1); complete = true; completedBytes = 1;
  reenterCancel = true; io->cancel();
  CHECK(resultCalls == 1);
  checkCompleted(io, "z", 1);
}

void trackedSynchronousErrorsAndReentrantSubmission()
{
  resetTracked(); readMode = 3;
  auto* io = new WindowsRestartIo(fakeApi());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed);
  const auto uncertain = io->settlement();
  CHECK(uncertain && uncertain->outcome == WindowsRestartIo::Settlement::Outcome::Uncertain);
  CHECK(io->consumeSettlement(uncertain->operation) && io->release());
  resetTracked(); io = new WindowsRestartIo(fakeApi()); reentrantIo = io; reenterRead = true;
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Pending);
  CHECK(readRetained && !io->release());
  resultError = ERROR_OPERATION_ABORTED;
  CHECK(io->cancel() == IoProgress::Failed);
  const auto aborted = io->settlement();
  CHECK(aborted && aborted->outcome == WindowsRestartIo::Settlement::Outcome::Aborted);
  CHECK(io->consumeSettlement(aborted->operation) && io->release());
}

void nestedPollCannotReuseActiveOperation()
{
  resetTracked(); auto* io = trackedRead(); reentrantIo = io;
  std::memcpy(pendingBuffer, "abc", 3); complete = true; completedBytes = 3;
  reenterResult = true;
  CHECK(io->poll() == IoProgress::Complete);
  CHECK(nestedBlocked && resultCalls == 2 && !io->settlement());
  CHECK(io->release());
}

void missingTransferCountIsUncertain()
{
  resetTracked(); auto* io = trackedRead();
  resultError = ERROR_OPERATION_ABORTED; leaveErrorCountUntouched = true;
  CHECK(io->cancel() == IoProgress::Failed);
  auto settled = io->settlement();
  CHECK(settled && settled->outcome == WindowsRestartIo::Settlement::Outcome::Uncertain);
  CHECK(io->consumeSettlement(settled->operation));
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed);
  CHECK(io->release());
  resetTracked(); readMode = 1; io = new WindowsRestartIo(fakeApi());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Failed);
  settled = io->settlement();
  CHECK(settled && settled->outcome == WindowsRestartIo::Settlement::Outcome::Uncertain);
  CHECK(io->consumeSettlement(settled->operation) && io->release());
  resetTracked(); readMode = 4; io = new WindowsRestartIo(fakeApi());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Eof);
  settled = io->settlement();
  CHECK(settled && settled->outcome == WindowsRestartIo::Settlement::Outcome::Eof);
  CHECK(io->consumeSettlement(settled->operation) && io->release());
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
  trackedCompletionBeforeCancel();
  trackedCancellationThenCompletion(ERROR_SUCCESS);
  trackedCancellationThenCompletion(ERROR_NOT_FOUND);
  trackedCancellationThenCompletion(ERROR_ACCESS_DENIED);
  trackedAbortAndUnknownExtent();
  trackedPartialReadsRemainDistinct();
  trackedTimeoutAndReentrantCompletion();
  trackedSynchronousErrorsAndReentrantSubmission();
  nestedPollCannotReuseActiveOperation();
  missingTransferCountIsUncertain();
}
