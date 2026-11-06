#include "fixture_restart/WindowsRestartChannel.h"
#include "fixture_restart/WindowsRestartProcess.h"
#include <cstdlib>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
HANDLE server = reinterpret_cast<HANDLE>(11), child = reinterpret_cast<HANDLE>(12);
DWORD lastError, closes;
ULONGLONG ticks;
bool connected, cancelled, failOpen, earlyConnection, delayedCancel, writeAsync, closeFailure;
void reset() { ticks = 1; lastError = closes = 0; connected = cancelled = failOpen = earlyConnection = delayedCancel = writeAsync = closeFailure = false; }
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
BOOL WINAPI close(HANDLE h) { ++closes; return !(closeFailure && h == reinterpret_cast<HANDLE>(11)); }
DWORD WINAPI error() { return lastError; }
ULONGLONG WINAPI now() { return ticks; }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
BOOL WINAPI writeIo(HANDLE, LPCVOID, DWORD size, LPDWORD count, LPOVERLAPPED)
{
  if (writeAsync) { lastError = ERROR_IO_PENDING; return FALSE; }
  *count = size; return TRUE;
}
HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES, LPCWSTR) { return reinterpret_cast<HANDLE>(30); }
BOOL WINAPI setJob(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD) { return TRUE; }
BOOL WINAPI initAttrs(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD, DWORD, PSIZE_T size)
{ *size = 256; return list != nullptr; }
BOOL WINAPI updateAttrs(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T)
{ return TRUE; }
void WINAPI deleteAttrs(LPPROC_THREAD_ATTRIBUTE_LIST) {}
BOOL WINAPI createProcess(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
    BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION info)
{ info->hProcess = reinterpret_cast<HANDLE>(31); info->hThread = reinterpret_cast<HANDLE>(32); return TRUE; }
BOOL WINAPI inJob(HANDLE, HANDLE, PBOOL member) { *member = TRUE; return TRUE; }
DWORD WINAPI resume(HANDLE) { return 1; }
DWORD WINAPI wait(HANDLE, DWORD) { return WAIT_OBJECT_0; }
BOOL WINAPI query(HANDLE, JOBOBJECTINFOCLASS, LPVOID value, DWORD, LPDWORD)
{ static_cast<JOBOBJECT_BASIC_ACCOUNTING_INFORMATION*>(value)->ActiveProcesses = 0; return TRUE; }
BOOL WINAPI exitCode(HANDLE, LPDWORD value) { *value = 0; return TRUE; }
WindowsProcessApi processApi()
{
  WindowsProcessApi a;
  a.createJob = createJob; a.setJob = setJob; a.initAttributes = initAttrs;
  a.updateAttribute = updateAttrs; a.deleteAttributes = deleteAttrs; a.createProcess = createProcess;
  a.inJob = inJob; a.resume = resume; a.wait = wait; a.queryJob = query; a.exitCode = exitCode;
  a.close = close; a.now = now;
  return a;
}
WindowsIoApi ioApi()
{
  WindowsIoApi a;
  a.createEvent = event; a.closeHandle = close; a.resetEvent = resetEvent;
  a.writeFile = writeIo; a.result = result; a.cancel = cancel; a.error = error; a.now = now;
  return a;
}
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
void closeOnlyCompletedGrant()
{
  reset(); server = reinterpret_cast<HANDLE>(11); child = reinterpret_cast<HANDLE>(12);
  auto* grant = WindowsRestartChannel::create(api());
  CHECK(grant->begin(10) == ChannelProgress::Pending); connected = true;
  CHECK(grant->poll() == ChannelProgress::Complete);
  server = reinterpret_cast<HANDLE>(21); child = reinterpret_cast<HANDLE>(22); connected = false;
  auto* control = WindowsRestartChannel::create(api());
  CHECK(control->begin(10) == ChannelProgress::Pending); connected = true;
  CHECK(control->poll() == ChannelProgress::Complete);
  auto* grantIo = new WindowsRestartIo(ioApi());
  auto* controlIo = new WindowsRestartIo(ioApi());
  auto* p = WindowsRestartProcess::createWithChannels(processApi(), {grant, control}, {grantIo, controlIo});
  CHECK(p && p->ownsChannel(grant) && p->ownsChannel(control) && !p->ownsChannel(nullptr));
  auto* unrelated = new WindowsRestartIo(ioApi());
  CHECK(p->ownsEndpoint(grant, grantIo) && p->ownsEndpoint(control, controlIo));
  CHECK(!p->ownsEndpoint(grant, controlIo) && !p->ownsEndpoint(control, grantIo));
  CHECK(!p->ownsEndpoint(grant, unrelated) && !p->ownsEndpoint(nullptr, grantIo));
  CHECK(unrelated->release());
  server = reinterpret_cast<HANDLE>(41); child = reinterpret_cast<HANDLE>(42); connected = false;
  auto* foreign = WindowsRestartChannel::create(api());
  CHECK(foreign->begin(10) == ChannelProgress::Pending); connected = true;
  CHECK(foreign->poll() == ChannelProgress::Complete);
  auto* foreignIo = new WindowsRestartIo(ioApi());
  auto* other = WindowsRestartProcess::createWithChannels(processApi(), {foreign}, {foreignIo});
  CHECK(other && !p->ownsEndpoint(foreign, foreignIo) && !other->ownsEndpoint(grant, grantIo));
  CHECK(other->release());
  server = reinterpret_cast<HANDLE>(21);
  CHECK(!p->launchEstablished() && !p->closeParentChannel(grant));
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Pending);
  const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  ProcessProgress state;
  do { state = p->pollLaunch(); std::this_thread::yield(); }
  while (state == ProcessProgress::Pending && std::chrono::steady_clock::now() < limit);
  CHECK(state == ProcessProgress::Complete && p->launchEstablished());
  CHECK(!p->closeParentChannel(grant));
  const char value = 'x';
  writeAsync = true;
  CHECK(grantIo->begin(grant->parentHandle(), false, &value, 1, 10) == IoProgress::Pending);
  CHECK(!p->closeParentChannel(grant));
  connected = false; cancelled = true;
  CHECK(grantIo->cancel() == IoProgress::Failed);
  writeAsync = false;
  CHECK(grantIo->begin(grant->parentHandle(), false, &value, 1, 10) == IoProgress::Complete);
  CHECK(grantIo->begin(grant->parentHandle(), false, &value, 0, 10) == IoProgress::Failed);
  CHECK(!p->closeParentChannel(grant));
  CHECK(grantIo->begin(grant->parentHandle(), false, &value, 1, 10) == IoProgress::Complete);
  writeAsync = true;
  CHECK(controlIo->begin(control->parentHandle(), false, &value, 1, 10) == IoProgress::Pending);
  closeFailure = true; CHECK(!p->closeParentChannel(grant)); closeFailure = false;
  CHECK(p->closeParentChannel(grant) && grant->parentHandle() == INVALID_HANDLE_VALUE);
  CHECK(p->ownsChannel(grant) && !p->ownsEndpoint(grant, grantIo));
  CHECK(p->ownsEndpoint(control, controlIo));
  CHECK(!p->closeParentChannel(grant) && control->parentHandle() == server);
  CHECK(grantIo->begin(reinterpret_cast<HANDLE>(11), false, &value, 1, 10) == IoProgress::Failed);
  CHECK(controlIo->cancel() == IoProgress::Failed);
  CHECK(p->pollHealthy(10) == ProcessProgress::Complete && p->release());
}
}
int main()
{
  connectedHandoff(); cancellationAndOpenFailure();
  rejectsEarlyAndLateConnection(); retainsUnconfirmedConnection();
  closeOnlyCompletedGrant();
}
