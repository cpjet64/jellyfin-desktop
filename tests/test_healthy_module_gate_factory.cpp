#include <fixture_restart/WindowsRestartChannel.h>
#include <fixture_restart/WindowsRestartProcess.h>
#include <healthy_restart/module_identity_win.h>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <thread>
using namespace FixtureRestart;
using namespace HealthyModuleIdentity;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
namespace {
int launches = 0;
int writes = 0;
bool fakeLaunch = false;
HANDLE WINAPI current() { return reinterpret_cast<HANDLE>(2); }
BOOL WINAPI token(HANDLE, DWORD, PHANDLE out) { *out = reinterpret_cast<HANDLE>(3); return TRUE; }
BOOL WINAPI tokenInfo(HANDLE, TOKEN_INFORMATION_CLASS, LPVOID data, DWORD, PDWORD size)
{ *size = sizeof(TOKEN_USER); if (!data) return FALSE; static_cast<TOKEN_USER*>(data)->User.Sid = reinterpret_cast<PSID>(4); return TRUE; }
BOOL WINAPI sid(PSID, LPWSTR* out) { static wchar_t value[] = L"S-1-5-21-1"; *out = value; return TRUE; }
BOOL WINAPI descriptor(LPCWSTR, DWORD, PSECURITY_DESCRIPTOR* out, PULONG)
{ *out = reinterpret_cast<PSECURITY_DESCRIPTOR>(5); return TRUE; }
HLOCAL WINAPI localFree(HLOCAL) { return nullptr; }
NTSTATUS WINAPI random(BCRYPT_ALG_HANDLE, PUCHAR out, ULONG size, ULONG)
{ std::memset(out, 7, size); return 0; }
HANDLE WINAPI pipe(LPCWSTR, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, LPSECURITY_ATTRIBUTES)
{ return reinterpret_cast<HANDLE>(11); }
HANDLE WINAPI file(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)
{ return reinterpret_cast<HANDLE>(12); }
HANDLE WINAPI event(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR) { return reinterpret_cast<HANDLE>(6); }
BOOL WINAPI connect(HANDLE, LPOVERLAPPED) { return FALSE; }
DWORD WINAPI error() { return ERROR_IO_PENDING; }
BOOL WINAPI result(HANDLE, LPOVERLAPPED, LPDWORD bytes, BOOL) { *bytes = 0; return TRUE; }
BOOL WINAPI close(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return 1; }
BOOL WINAPI createProcess(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
                         BOOL, DWORD, LPVOID, LPCWSTR, LPSTARTUPINFOW, LPPROCESS_INFORMATION info)
{ ++launches; if (!fakeLaunch) return FALSE; info->hProcess = reinterpret_cast<HANDLE>(31); info->hThread = reinterpret_cast<HANDLE>(32); return TRUE; }
HANDLE WINAPI job(LPSECURITY_ATTRIBUTES, LPCWSTR) { return reinterpret_cast<HANDLE>(30); }
BOOL WINAPI setJob(HANDLE, JOBOBJECTINFOCLASS, LPVOID, DWORD) { return TRUE; }
BOOL WINAPI attrs(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD, DWORD, PSIZE_T size)
{ *size = 256; return list != nullptr; }
BOOL WINAPI update(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR, PVOID, SIZE_T, PVOID, PSIZE_T) { return TRUE; }
void WINAPI deleteAttrs(LPPROC_THREAD_ATTRIBUTE_LIST) {}
BOOL WINAPI inJob(HANDLE, HANDLE, PBOOL member) { *member = TRUE; return TRUE; }
DWORD WINAPI resume(HANDLE) { return 1; }
DWORD WINAPI wait(HANDLE, DWORD) { return WAIT_OBJECT_0; }
BOOL WINAPI query(HANDLE, JOBOBJECTINFOCLASS, LPVOID data, DWORD, LPDWORD)
{ static_cast<JOBOBJECT_BASIC_ACCOUNTING_INFORMATION*>(data)->ActiveProcesses = 0; return TRUE; }
BOOL WINAPI exitCode(HANDLE, LPDWORD code) { *code = 0; return TRUE; }
BOOL WINAPI duplicate(HANDLE, HANDLE, HANDLE, LPHANDLE out, DWORD, BOOL, DWORD)
{ *out = reinterpret_cast<HANDLE>(14); return TRUE; }
DWORD WINAPI pid() { return 19; }
DWORD WINAPI processId(HANDLE) { return 19; }
BOOL WINAPI handleInfo(HANDLE, LPDWORD flags) { *flags = HANDLE_FLAG_INHERIT; return TRUE; }
BOOL WINAPI writeIo(HANDLE, LPCVOID, DWORD size, LPDWORD written, LPOVERLAPPED)
{ ++writes; *written = size; return TRUE; }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
WindowsProcessApi processApi()
{
  WindowsProcessApi a; a.createProcess = createProcess; a.close = close; a.now = now;
  a.duplicate = duplicate; a.currentProcess = current; a.currentProcessId = pid;
  a.processId = processId; a.handleInfo = handleInfo; a.createJob = job; a.setJob = setJob;
  a.initAttributes = attrs; a.updateAttribute = update; a.deleteAttributes = deleteAttrs;
  a.inJob = inJob; a.resume = resume; a.wait = wait; a.queryJob = query; a.exitCode = exitCode;
  return a;
}
WindowsChannelApi channelApi()
{
  WindowsChannelApi a; a.currentProcess = current; a.openToken = token; a.tokenInfo = tokenInfo;
  a.sidString = sid; a.descriptor = descriptor; a.localFree = localFree; a.random = random;
  a.createPipe = pipe; a.createFile = file; a.event = event; a.connect = connect;
  a.result = result; a.close = close; a.error = error; a.now = now; return a;
}
void noLeaseCannotLaunch()
{
  auto* channel = WindowsRestartChannel::create(channelApi()); CHECK(channel);
  CHECK(channel->begin(100) == ChannelProgress::Complete);
  WindowsIoApi ioApi; ioApi.createEvent = event; ioApi.closeHandle = close;
  auto* io = new WindowsRestartIo(ioApi);
  auto* process = WindowsRestartProcess::createHealthyWithChannels(processApi(), {channel}, {io}, channel); CHECK(process);
  std::shared_ptr<StageLease> absent;
  CHECK(!process->bindHealthyModuleLease(absent, channel, 1));
  CHECK(!process->bindHealthyModuleLease(absent, nullptr, 1));
  FileObservation output; output.size = 123; std::uint64_t launchId = 7;
  CHECK(!process->healthyModuleEvidence(channel, output, launchId));
  CHECK(output.size == 123 && launchId == 7);
  CHECK(process->launch(L"C:\\fixture\\child.exe", L"", 100) == ProcessProgress::Failed);
  CHECK(launches == 0 && process->release());
}
Digest selectedForTest(const Digest&) { return expectedDigest(); }
BOOL WINAPI failedRead(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED) { return FALSE; }
void leaseRetainedUntilOwnerRelease()
{
  CHECK(CreateDirectoryW(L"owned-stage", nullptr));
  HANDLE directory = CreateFileW(L"owned-stage", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
  CHECK(directory != INVALID_HANDLE_VALUE);
  HANDLE fileHandle = CreateFileW(L"owned-stage\\libmpv-2.dll", GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(fileHandle != INVALID_HANDLE_VALUE);
  DWORD written = 0; CHECK(WriteFile(fileHandle, "fixture", 7, &written, nullptr) && written == 7);
  CHECK(CloseHandle(fileHandle));
  StageApi stageApi; stageApi.overrideDigest = selectedForTest;
  NativeError errorValue = NativeError::Unavailable;
  StageApi failure = stageApi; failure.read = failedRead;
  CHECK(!StageLease::acquireForTest(reinterpret_cast<std::uintptr_t>(directory), failure, errorValue));
  CHECK(errorValue == NativeError::Read);
  auto unique = StageLease::acquireForTest(reinterpret_cast<std::uintptr_t>(directory), stageApi, errorValue);
  CHECK(unique && errorValue == NativeError::None);
  std::shared_ptr<StageLease> lease(std::move(unique)); std::weak_ptr<StageLease> weak = lease;
  auto* channel = WindowsRestartChannel::create(channelApi()); CHECK(channel);
  CHECK(channel->begin(100) == ChannelProgress::Complete);
  WindowsIoApi ioApi; ioApi.createEvent = event; ioApi.closeHandle = close; ioApi.now = now;
  ioApi.writeFile = writeIo; ioApi.resetEvent = resetEvent;
  auto* io = new WindowsRestartIo(ioApi);
  auto* process = WindowsRestartProcess::createHealthyWithChannels(processApi(), {channel}, {io}, channel); CHECK(process);
  auto* borrowed = WindowsRestartChannel::create(channelApi()); CHECK(borrowed);
  CHECK(!process->bindHealthyModuleLease(lease, borrowed, 11) && lease);
  CHECK(!process->bindHealthyModuleLease(lease, channel, 0) && lease);
  CHECK(process->bindHealthyModuleLease(lease, channel, 11) && !lease && !weak.expired());
  const char byte = 'x';
  CHECK(io->begin(channel->parentHandle(), false, &byte, 1, 100) == IoProgress::Failed);
  CHECK(io->poll() == IoProgress::Failed && io->cancel() == IoProgress::Failed && writes == 0);
  CHECK(process->sendHealthyModuleFrame(100) == ProcessProgress::Failed && writes == 0);
  FileObservation observation; std::uint64_t launchId = 0;
  CHECK(!process->healthyModuleEvidence(borrowed, observation, launchId));
  CHECK(process->healthyModuleEvidence(channel, observation, launchId));
  CHECK(launchId == 11 && observation.size == 7 && observation.hashComplete);
  fileHandle = CreateFileW(L"owned-stage\\libmpv-2.dll", GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(fileHandle == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION);
  fakeLaunch = true;
  CHECK(process->launch(L"C:\\fixture\\child.exe", L"", 100) == ProcessProgress::Pending);
  ProcessProgress status = ProcessProgress::Pending;
  for (int i = 0; i < 100000 && status == ProcessProgress::Pending; ++i) {
    status = process->pollLaunch(); std::this_thread::yield();
  }
  CHECK(status == ProcessProgress::Complete && launches == 1);
  CHECK(process->sendHealthyModuleFrame(100) == ProcessProgress::Complete && writes == 1);
  CHECK(process->sendHealthyModuleFrame(100) == ProcessProgress::Failed && writes == 1);
  CHECK(!weak.expired() && process->pollHealthy(100) == ProcessProgress::Complete);
  CHECK(borrowed->release() && process->release() && weak.expired());
  fileHandle = CreateFileW(L"owned-stage\\libmpv-2.dll", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
  CHECK(fileHandle != INVALID_HANDLE_VALUE && CloseHandle(fileHandle));
  CHECK(CloseHandle(directory));
  CHECK(DeleteFileW(L"owned-stage\\libmpv-2.dll") && RemoveDirectoryW(L"owned-stage"));
}
void genericIoRemainsUsable()
{
  WindowsIoApi api; api.createEvent = event; api.closeHandle = close; api.now = now;
  api.writeFile = writeIo; api.resetEvent = resetEvent;
  auto* io = new WindowsRestartIo(api); const char byte = 'x';
  CHECK(io->begin(reinterpret_cast<HANDLE>(99), false, &byte, 1, 100) == IoProgress::Complete);
  CHECK(writes == 2 && io->release());
}
}
int main() { noLeaseCannotLaunch(); leaseRetainedUntilOwnerRelease(); genericIoRemainsUsable(); }
