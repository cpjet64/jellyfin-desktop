#include "fixture_restart/WindowsRestartIo.h"
#include <cstdlib>
#include <cstring>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
bool authority = true;
bool synchronousRead = false;
int reads = 0, cancels = 0, results = 0;
void* buffer = nullptr;
DWORD error = ERROR_IO_PENDING;
ULONGLONG tick = 1;
int resultMode = 0;
HANDLE WINAPI eventFake(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(1); }
BOOL WINAPI closeFake(HANDLE) { return TRUE; }
BOOL WINAPI resetFake(HANDLE) { return TRUE; }
DWORD WINAPI errorFake() { return error; }
ULONGLONG WINAPI clockFake() { return tick; }
BOOL WINAPI readFake(HANDLE, LPVOID out, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (synchronousRead) { std::memset(out, 'x', size); *count = size; return TRUE; }
  ++reads; buffer = out; error = ERROR_IO_PENDING; return FALSE;
}
BOOL WINAPI writeFake(HANDLE, LPCVOID, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  *count = size; return TRUE;
}
BOOL WINAPI cancelFake(HANDLE, LPOVERLAPPED)
{
  ++cancels; authority = false; return TRUE;
}
BOOL WINAPI resultFake(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{
  if (resultMode == 1) { error = ERROR_IO_INCOMPLETE; return FALSE; }
  if (resultMode == 2) { error = ERROR_INVALID_HANDLE; return FALSE; }
  if (resultMode == 3) { *count = 0; error = ERROR_OPERATION_ABORTED; return FALSE; }
  ++results; std::memcpy(buffer, "abc", 3); *count = 3; return TRUE;
}
WindowsIoApi api()
{
  WindowsIoApi result;
  result.createEvent = eventFake; result.closeHandle = closeFake; result.resetEvent = resetFake;
  result.readFile = readFake; result.writeFile = writeFake; result.cancel = cancelFake; result.result = resultFake;
  result.error = errorFake; result.now = clockFake;
  return result;
}
void cancellationRace()
{
  auto* io = new WindowsRestartIo(api());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 8, 10) == IoProgress::Pending);
  CHECK(io->cancelGuarded([] { return authority; }) == IoProgress::Failed);
  const auto result = io->settlement();
  CHECK(!authority && reads == 1 && cancels == 1 && results == 1);
  CHECK(result && result->outcome == WindowsRestartIo::Settlement::Outcome::Completed);
  CHECK(result->read && result->requested == 8 && result->transferred == 3 && result->bytes.size() == 3);
  CHECK(std::memcmp(result->bytes.data(), "abc", 3) == 0 && !io->release());
  CHECK(io->consumeSettlement(result->operation) && !io->consumeSettlement(result->operation));
  CHECK(io->release() && result->transferred == 3 && std::memcmp(result->bytes.data(), "abc", 3) == 0);
}
void completedWriteSnapshot()
{
  authority = true; auto* io = new WindowsRestartIo(api()); const char bytes[] = "hello";
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), false, bytes, 5, 10) == IoProgress::Complete);
  CHECK(io->retainCompleted() && !io->retainCompleted());
  const auto result = io->settlement();
  CHECK(result && !result->read && result->requested == 5 && result->transferred == 5);
  CHECK(result->bytes.size() == 5 && std::memcmp(result->bytes.data(), bytes, 5) == 0);
  CHECK(!io->release() && io->consumeSettlement(result->operation));
  CHECK(!io->consumeSettlement(result->operation) && !io->retainCompleted() && io->release());
  CHECK(std::memcmp(result->bytes.data(), bytes, 5) == 0);
}
void guardedTrackedRead()
{
  authority = true; reads = 0; auto* io = new WindowsRestartIo(api());
  CHECK(io->beginTrackedReadGuarded(reinterpret_cast<HANDLE>(2), 8, 10, [] { return authority; }) == IoProgress::Pending);
  authority = false;
  CHECK(io->pollGuarded([] { return authority; }) == IoProgress::Complete);
  const auto result = io->settlement();
  CHECK(result && result->read && result->transferred == 3 && reads == 1 && !io->retainCompleted());
  CHECK(std::memcmp(result->bytes.data(), "abc", 3) == 0);
  CHECK(io->consumeSettlement(result->operation) && !io->retainCompleted() && io->release());
  synchronousRead = true; io = new WindowsRestartIo(api());
  CHECK(io->begin(reinterpret_cast<HANDLE>(2), true, nullptr, 2, 10) == IoProgress::Complete);
  CHECK(!io->retainCompleted() && !io->settlement() && io->release());
}
void incompleteCancellationAndDeadline()
{
  tick = 1; resultMode = 1; synchronousRead = false; auto* io = new WindowsRestartIo(api());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Pending);
  const auto operation = io->operation();
  CHECK(io->cancel() == IoProgress::Pending && io->pending() && !io->release());
  CHECK(!io->settlement() && io->operation() == operation);
  tick = 10;
  CHECK(io->poll() == IoProgress::Unconfirmed && io->pending() && !io->release());
  CHECK(!io->settlement() && io->operation() == operation);
  resultMode = 3;
  CHECK(io->pollSettlementOnly() == IoProgress::Failed);
  const auto settled = io->settlement();
  CHECK(settled && settled->operation == operation && settled->outcome == WindowsRestartIo::Settlement::Outcome::Aborted);
  CHECK(io->consumeSettlement(operation) && io->release());
}
void invalidHandleIsUnconfirmed()
{
  tick = 1; resultMode = 2; synchronousRead = false; auto* io = new WindowsRestartIo(api());
  CHECK(io->beginTrackedRead(reinterpret_cast<HANDLE>(2), 8, 10) == IoProgress::Pending);
  CHECK(io->cancel() == IoProgress::Unconfirmed && io->pending() && !io->release());
  CHECK(!io->settlement());
  resultMode = 3;
  (void)io->pollSettlementOnly();
  const auto settled = io->settlement(); CHECK(settled);
  CHECK(io->consumeSettlement(settled->operation) && io->release());
}
}
int main() { cancellationRace(); completedWriteSnapshot(); guardedTrackedRead(); incompleteCancellationAndDeadline(); invalidHandleIsUnconfirmed(); }
