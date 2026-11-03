#include <windows.h>
#include <cstdio>
#include <string>

DWORD waitForJob(HANDLE job)
{
  JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
  for (int i = 0; i < 50; ++i) {
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation, &accounting, sizeof(accounting), nullptr))
      return static_cast<DWORD>(-1);
    if (accounting.ActiveProcesses == 0) return 0;
    Sleep(100);
  }
  return accounting.ActiveProcesses;
}

int wmain(int argc, wchar_t **argv)
{
  if (argc < 3 || argc > 4) return 2;
  const std::wstring name = L"JellyfinSmoke-" + std::to_wstring(GetCurrentProcessId());
  HDESK desktop = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
  if (!desktop) { std::fprintf(stderr, "CreateDesktop failed: %lu\n", GetLastError()); return 3; }
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    std::fprintf(stderr, "Job setup failed: %lu\n", GetLastError());
    if (job) CloseHandle(job);
    CloseDesktop(desktop);
    return 4;
  }
  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\" --config-dir \"" + argv[2] + L"\"";
  if (argc == 4) command += L" " + std::wstring(argv[3]);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = const_cast<wchar_t *>(name.c_str());
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(argv[1], &command[0], nullptr, nullptr, TRUE, CREATE_SUSPENDED,
                      nullptr, nullptr, &startup, &process)) {
    std::fprintf(stderr, "CreateProcess failed: %lu\n", GetLastError());
    CloseHandle(job); CloseDesktop(desktop); return 5;
  }
  if (!AssignProcessToJobObject(job, process.hProcess)) {
    std::fprintf(stderr, "AssignProcessToJobObject failed: %lu\n", GetLastError());
    TerminateProcess(process.hProcess, 1);
    WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); CloseDesktop(desktop);
    return 6;
  }
  std::printf("app pid %lu\n", process.dwProcessId); std::fflush(stdout);
  if (ResumeThread(process.hThread) == static_cast<DWORD>(-1)) {
    std::fprintf(stderr, "ResumeThread failed: %lu\n", GetLastError());
    CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); CloseDesktop(desktop);
    return 8;
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, 12000);
  DWORD exitCode = 0;
  GetExitCodeProcess(process.hProcess, &exitCode);
  std::printf("app wait %lu exit %lu\n", wait, exitCode); std::fflush(stdout);
  DWORD active = waitForJob(job);
  const bool success = wait == WAIT_OBJECT_0 && exitCode == 0 && active == 0;
  const bool forced = !success;
  if (forced) {
    TerminateJobObject(job, 1);
    active = waitForJob(job);
  }
  std::printf("forcedCleanup %s; job active processes %lu\n", forced ? "true" : "false", active);
  std::fflush(stdout);
  CloseHandle(process.hThread); CloseHandle(process.hProcess); CloseHandle(job); CloseDesktop(desktop);
  return success ? 0 : 7;
}
