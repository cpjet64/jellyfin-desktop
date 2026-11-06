#include "fixture_restart/WindowsRestartIo.h"
#include <cstdlib>
#include <cstring>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
bool authority = true;
int reads = 0, cancels = 0, results = 0;
void* buffer = nullptr;
DWORD error = ERROR_IO_PENDING;
HANDLE WINAPI eventFake(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(1); }
BOOL WINAPI closeFake(HANDLE) { return TRUE; }
BOOL WINAPI resetFake(HANDLE) { return TRUE; }
DWORD WINAPI errorFake() { return error; }
ULONGLONG WINAPI clockFake() { return 1; }
BOOL WINAPI readFake(HANDLE, LPVOID out, DWORD, LPDWORD, LPOVERLAPPED)
{
  ++reads; buffer = out; error = ERROR_IO_PENDING; return FALSE;
}
BOOL WINAPI cancelFake(HANDLE, LPOVERLAPPED)
{
  ++cancels; authority = false; return TRUE;
}
BOOL WINAPI resultFake(HANDLE, LPOVERLAPPED, LPDWORD count, BOOL)
{
  ++results; std::memcpy(buffer, "abc", 3); *count = 3; return TRUE;
}
WindowsIoApi api()
{
  WindowsIoApi result;
  result.createEvent = eventFake; result.closeHandle = closeFake; result.resetEvent = resetFake;
  result.readFile = readFake; result.cancel = cancelFake; result.result = resultFake;
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
}
int main() { cancellationRace(); }
