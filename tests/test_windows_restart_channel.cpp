#include "fixture_restart/WindowsRestartChannel.h"
#include "fixture_restart/WindowsRestartProcess.h"
#include <cstdlib>
#include <cstring>
#include <string>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
HANDLE server = reinterpret_cast<HANDLE>(11), child = reinterpret_cast<HANDLE>(12);
DWORD lastError, closes;
ULONGLONG ticks;
bool connected, cancelled, failOpen, earlyConnection, delayedCancel;
void reset() { ticks = 1; lastError = closes = 0; connected = cancelled = failOpen = earlyConnection = delayedCancel = false; }
HANDLE WINAPI current() { return reinterpret_cast<HANDLE>(2); }
BOOL WINAPI openToken(HANDLE, DWORD, PHANDLE out) { *out = reinterpret_cast<HANDLE>(3); return TRUE; }
BOOL WINAPI tokenInfo(HANDLE, TOKEN_INFORMATION_CLASS kind, LPVOID data, DWORD, PDWORD size)
{
  CHECK(kind == TokenUser); *size = sizeof(TOKEN_USER);
  if (!data) return FALSE;
  static_cast<TOKEN_USER*>(data)->User.Sid = reinterpret_cast<PSID>(4);
  return TRUE;
}
BOOL WINAPI sidString(PSID, LPWSTR* sid) { static wchar_t value[] = L"S-1-5-21-1"; *sid = value; return TRUE; }
BOOL WINAPI descriptor(LPCWSTR value, DWORD, PSECURITY_DESCRIPTOR* out, PULONG)
{
  CHECK(std::wstring(value) == L"D:P(A;;GA;;;S-1-5-21-1)(A;;GA;;;SY)");
  *out = reinterpret_cast<PSECURITY_DESCRIPTOR>(5); return TRUE;
}
HLOCAL WINAPI localFree(HLOCAL) { return nullptr; }
NTSTATUS WINAPI random(BCRYPT_ALG_HANDLE, PUCHAR bytes, ULONG size, ULONG flags)
{ CHECK(size == 16 && flags == BCRYPT_USE_SYSTEM_PREFERRED_RNG); std::memset(bytes, 7, size); return 0; }
HANDLE WINAPI pipe(LPCWSTR name, DWORD access, DWORD mode, DWORD instances, DWORD, DWORD,
                   DWORD, LPSECURITY_ATTRIBUTES security)
{
  CHECK(std::wstring(name).find(L"\\\\.\\pipe\\jellyfin-restart-") == 0);
  CHECK((access & (FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE)) ==
        (FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE));
  CHECK((mode & PIPE_REJECT_REMOTE_CLIENTS) && instances == 1 && !security->bInheritHandle);
  return server;
}
HANDLE WINAPI file(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES security, DWORD, DWORD flags, HANDLE)
{ CHECK(security->bInheritHandle && (flags & FILE_FLAG_OVERLAPPED)); return failOpen ? INVALID_HANDLE_VALUE : child; }
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(6); }
BOOL WINAPI connect(HANDLE h, LPOVERLAPPED overlap)
{
  CHECK(h == server && overlap && overlap->hEvent);
  if (earlyConnection) return TRUE;
  lastError = ERROR_IO_PENDING; return FALSE;
}
BOOL WINAPI result(HANDLE, LPOVERLAPPED, LPDWORD bytes, BOOL)
{
  if (connected) { *bytes = 0; return TRUE; }
  lastError = cancelled && !delayedCancel ? ERROR_OPERATION_ABORTED : ERROR_IO_INCOMPLETE;
  return FALSE;
}
BOOL WINAPI cancel(HANDLE, LPOVERLAPPED) { cancelled = true; return TRUE; }
BOOL WINAPI close(HANDLE) { ++closes; return TRUE; }
DWORD WINAPI error() { return lastError; }
ULONGLONG WINAPI now() { return ticks; }
WindowsChannelApi api()
{
  WindowsChannelApi a;
  a.currentProcess = current; a.openToken = openToken; a.tokenInfo = tokenInfo;
  a.sidString = sidString; a.descriptor = descriptor; a.localFree = localFree; a.random = random;
  a.createPipe = pipe; a.createFile = file; a.event = event; a.connect = connect;
  a.result = result; a.cancel = cancel; a.close = close; a.error = error; a.now = now;
  return a;
}
void connectedHandoff()
{
  reset(); auto* c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Pending && !c->release());
  connected = true;
  CHECK(c->poll() == ChannelProgress::Complete && c->parentHandle() == server && c->childHandle() == child);
  WindowsProcessApi processApi; processApi.close = close;
  auto* io = new WindowsRestartIo(WindowsIoApi{});
  auto* p = WindowsRestartProcess::createWithChannels(processApi, {c}, {io});
  CHECK(p && !c->release() && c->childHandle() == INVALID_HANDLE_VALUE);
  CHECK(!io->canBind(child) && !io->release());
  CHECK(p->release());
}
void cancellationAndOpenFailure()
{
  reset(); auto* c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Pending);
  CHECK(c->cancel() == ChannelProgress::Failed && cancelled && c->release());
  reset(); failOpen = true; c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Failed && cancelled && c->release());
}
void rejectsEarlyAndLateConnection()
{
  reset(); earlyConnection = true; auto* c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Failed && c->release());
  reset(); c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Pending);
  ticks = 10; connected = true;
  CHECK(c->poll() == ChannelProgress::Failed && c->release());
}
void retainsUnconfirmedConnection()
{
  reset(); delayedCancel = true; auto* c = WindowsRestartChannel::create(api());
  CHECK(c->begin(10) == ChannelProgress::Pending);
  CHECK(c->cancel() == ChannelProgress::Unconfirmed && !c->release());
  delayedCancel = false;
  CHECK(c->poll() == ChannelProgress::Failed && c->release());
}
}
int main()
{
  connectedHandoff(); cancellationAndOpenFailure();
  rejectsEarlyAndLateConnection(); retainsUnconfirmedConnection();
}
