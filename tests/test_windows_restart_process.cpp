#include "fixture_restart/WindowsRestartProcess.h"
#include <cstdlib>
#include <vector>
using namespace FixtureRestart;
#define CHECK(condition) do { if (!(condition)) std::abort(); } while (false)

namespace {
HANDLE job = reinterpret_cast<HANDLE>(11), process = reinterpret_cast<HANDLE>(12);
HANDLE thread = reinterpret_cast<HANDLE>(13), child = reinterpret_cast<HANDLE>(14);
bool createOk, membershipOk, member, resumeOk, signaled, terminated;
DWORD active, exitValue, attributes, creates, resumes;
DWORD ioError;
bool ioDone;
ULONGLONG ticks;
void reset()
{
  createOk = membershipOk = member = resumeOk = true;
  signaled = terminated = false;
  active = 1; exitValue = 0; attributes = creates = resumes = 0; ticks = 1;
}
HANDLE WINAPI createJob(LPSECURITY_ATTRIBUTES, LPCWSTR) { return job; }
BOOL WINAPI setJob(HANDLE h, JOBOBJECTINFOCLASS type, LPVOID value, DWORD)
{
  CHECK(h == job && type == JobObjectExtendedLimitInformation);
  CHECK(static_cast<JOBOBJECT_EXTENDED_LIMIT_INFORMATION*>(value)->BasicLimitInformation.LimitFlags ==
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE);
  return TRUE;
}
BOOL WINAPI init(LPPROC_THREAD_ATTRIBUTE_LIST list, DWORD count, DWORD, PSIZE_T size)
{ CHECK(count == 2); *size = 256; return list != nullptr; }
BOOL WINAPI update(LPPROC_THREAD_ATTRIBUTE_LIST, DWORD, DWORD_PTR kind,
                   PVOID value, SIZE_T size, PVOID, PSIZE_T)
{
  if (kind == PROC_THREAD_ATTRIBUTE_HANDLE_LIST) {
    CHECK(size == sizeof(HANDLE) && *static_cast<HANDLE*>(value) == child);
    attributes |= 1;
  } else {
    CHECK(kind == PROC_THREAD_ATTRIBUTE_JOB_LIST && size == sizeof(HANDLE));
    CHECK(*static_cast<HANDLE*>(value) == job);
    attributes |= 2;
  }
  return TRUE;
}
void WINAPI deleteAttrs(LPPROC_THREAD_ATTRIBUTE_LIST) {}
BOOL WINAPI createProcess(LPCWSTR, LPWSTR, LPSECURITY_ATTRIBUTES, LPSECURITY_ATTRIBUTES,
    BOOL inherit, DWORD flags, LPVOID, LPCWSTR, LPSTARTUPINFOW startup, LPPROCESS_INFORMATION out)
{
  ++creates;
  CHECK(attributes == 3 && inherit && startup->cb == sizeof(STARTUPINFOEXW));
  CHECK((flags & (EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW)) ==
        (EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED | CREATE_NO_WINDOW));
  if (!createOk) return FALSE;
  out->hProcess = process; out->hThread = thread;
  return TRUE;
}
BOOL WINAPI inJob(HANDLE p, HANDLE j, PBOOL result)
{ CHECK(p == process && j == job); *result = member; return membershipOk; }
DWORD WINAPI resume(HANDLE h) { CHECK(h == thread); ++resumes; return resumeOk ? 1 : DWORD(-1); }
BOOL WINAPI terminateJob(HANDLE h, UINT) { CHECK(h == job); terminated = true; return TRUE; }
BOOL WINAPI terminateProcess(HANDLE h, UINT) { CHECK(h == process); terminated = true; return TRUE; }
DWORD WINAPI wait(HANDLE h, DWORD timeout)
{ CHECK(h == process && timeout == 0); return signaled ? WAIT_OBJECT_0 : WAIT_TIMEOUT; }
BOOL WINAPI query(HANDLE h, JOBOBJECTINFOCLASS type, LPVOID value, DWORD, LPDWORD)
{
  CHECK(h == job && type == JobObjectBasicAccountingInformation);
  static_cast<JOBOBJECT_BASIC_ACCOUNTING_INFORMATION*>(value)->ActiveProcesses = active;
  return TRUE;
}
BOOL WINAPI exitCode(HANDLE h, LPDWORD value)
{ CHECK(h == process); *value = exitValue; return TRUE; }
BOOL WINAPI closeHandle(HANDLE) { return TRUE; }
ULONGLONG WINAPI now() { return ticks; }
HANDLE WINAPI createEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCWSTR)
{ return reinterpret_cast<HANDLE>(15); }
BOOL WINAPI resetEvent(HANDLE) { return TRUE; }
BOOL WINAPI readPending(HANDLE, LPVOID, DWORD, LPDWORD, LPOVERLAPPED)
{ ioError = ERROR_IO_PENDING; return FALSE; }
BOOL WINAPI ioResult(HANDLE, LPOVERLAPPED, LPDWORD, BOOL)
{ ioError = ioDone ? ERROR_OPERATION_ABORTED : ERROR_IO_INCOMPLETE; return FALSE; }
BOOL WINAPI cancelIo(HANDLE, LPOVERLAPPED) { return TRUE; }
DWORD WINAPI lastError() { return ioError; }
WindowsProcessApi api()
{
  WindowsProcessApi a;
  a.createJob = createJob; a.setJob = setJob; a.initAttributes = init;
  a.updateAttribute = update; a.deleteAttributes = deleteAttrs; a.createProcess = createProcess;
  a.inJob = inJob; a.resume = resume; a.terminateJob = terminateJob;
  a.terminateProcess = terminateProcess; a.wait = wait; a.queryJob = query;
  a.exitCode = exitCode; a.close = closeHandle; a.now = now;
  return a;
}
WindowsRestartProcess* fixture()
{ auto* p = WindowsRestartProcess::create(api(), {child}, {}); CHECK(p); return p; }
void createFailure()
{
  reset(); createOk = false; auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Failed);
  CHECK(creates == 1 && resumes == 0 && p->release());
}
void membershipFailure()
{
  reset(); membershipOk = false; auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Unconfirmed);
  CHECK(terminated && resumes == 0 && !p->release());
  signaled = true; active = 0;
  CHECK(p->forceCleanup(10) == ProcessProgress::Complete && p->release());
}
void resumeFailure()
{
  reset(); resumeOk = false; auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Unconfirmed);
  CHECK(terminated && resumes == 1 && p->pollHealthy(10) == ProcessProgress::Failed);
  signaled = true; active = 0;
  CHECK(p->forceCleanup(10) == ProcessProgress::Complete && p->release());
}
void healthyRequiresJobEmpty()
{
  reset(); auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Complete);
  signaled = true;
  CHECK(p->pollHealthy(10) == ProcessProgress::Pending && !p->release());
  active = 0;
  CHECK(p->pollHealthy(10) == ProcessProgress::Complete && p->release());
}
void forcedIsNotHealthy()
{
  reset(); auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Complete);
  CHECK(p->forceCleanup(10) == ProcessProgress::Pending && terminated);
  CHECK(p->pollHealthy(10) == ProcessProgress::Failed);
  signaled = true; active = 0;
  CHECK(p->forceCleanup(10) == ProcessProgress::Complete && p->release());
}
void retainsPendingIo()
{
  reset(); ioDone = false;
  WindowsIoApi ioApi;
  ioApi.createEvent = createEvent; ioApi.closeHandle = closeHandle;
  ioApi.resetEvent = resetEvent; ioApi.readFile = readPending;
  ioApi.result = ioResult; ioApi.cancel = cancelIo; ioApi.error = lastError; ioApi.now = now;
  auto* io = new WindowsRestartIo(ioApi);
  CHECK(io->begin(reinterpret_cast<HANDLE>(16), true, nullptr, 4, 10) == IoProgress::Pending);
  auto* p = WindowsRestartProcess::create(api(), {child}, {io});
  CHECK(p && p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Complete);
  signaled = true; active = 0;
  CHECK(p->pollHealthy(10) == ProcessProgress::Complete && !p->release());
  CHECK(io->cancel() == IoProgress::Unconfirmed);
  ioDone = true;
  CHECK(io->poll() == IoProgress::Failed && p->release());
}
void unhealthyExitAndDeadline()
{
  reset(); auto* p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Complete);
  signaled = true; active = 0; exitValue = 1;
  CHECK(p->pollHealthy(10) == ProcessProgress::Failed && p->release());
  reset(); p = fixture();
  CHECK(p->launch(L"C:\\fixture.exe", L"", 10) == ProcessProgress::Complete);
  ticks = 10;
  CHECK(p->pollHealthy(10) == ProcessProgress::Unconfirmed && !p->release());
  signaled = true; active = 0;
  CHECK(p->forceCleanup(10) == ProcessProgress::Failed && p->release());
}
}
int main()
{
  createFailure(); membershipFailure(); resumeFailure();
  healthyRequiresJobEmpty(); forcedIsNotHealthy();
  retainsPendingIo(); unhealthyExitAndDeadline();
}
