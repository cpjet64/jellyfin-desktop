#include <windows.h>
#include <cstdio>
#include <string>

struct WindowSearch { DWORD pid; HWND window; };

BOOL CALLBACK findWindow(HWND window, LPARAM param)
{
  auto* search = reinterpret_cast<WindowSearch*>(param);
  DWORD pid = 0;
  GetWindowThreadProcessId(window, &pid);
  if (pid == search->pid && IsWindowVisible(window) && GetWindow(window, GW_OWNER) == nullptr) {
    search->window = window;
    return FALSE;
  }
  return TRUE;
}

bool waitMaximized(HWND window, bool expected)
{
  for (int i = 0; i < 50; ++i) {
    WINDOWPLACEMENT placement{sizeof(placement)};
    if (GetWindowPlacement(window, &placement) &&
        (placement.showCmd == SW_SHOWMAXIMIZED) == expected) return true;
    Sleep(100);
  }
  return false;
}

bool waitFullscreen(HWND window)
{
  for (int i = 0; i < 50; ++i) {
    RECT rect{};
    MONITORINFO monitor{sizeof(monitor)};
    HMONITOR screen = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    if (GetWindowRect(window, &rect) && GetMonitorInfoW(screen, &monitor) &&
        EqualRect(&rect, &monitor.rcMonitor)) return true;
    Sleep(100);
  }
  return false;
}

void sendShortcut(HWND window, bool alt)
{
  const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
  const DWORD ownThread = GetCurrentThreadId();
  const bool attached = AttachThreadInput(ownThread, targetThread, TRUE) != FALSE;
  BYTE state[256]{};
  if (attached && alt) {
    GetKeyboardState(state);
    state[VK_MENU] |= 0x80;
    SetKeyboardState(state);
  }
  const WPARAM key = alt ? VK_RETURN : VK_F11;
  PostMessageW(window, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, key, alt ? 0x20000001 : 1);
  Sleep(250);
  PostMessageW(window, alt ? WM_SYSKEYUP : WM_KEYUP, key, alt ? 0xE0000001 : 0xC0000001);
  Sleep(250);
  if (attached && alt) {
    state[VK_MENU] &= 0x7f;
    SetKeyboardState(state);
  }
  if (attached) AttachThreadInput(ownThread, targetThread, FALSE);
}

bool checkWindowActions(HDESK desktop, DWORD pid)
{
  WindowSearch search{pid, nullptr};
  for (int i = 0; i < 100 && !search.window; ++i) {
    EnumDesktopWindows(desktop, findWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.window) Sleep(100);
  }
  HDESK previous = GetThreadDesktop(GetCurrentThreadId());
  if (!search.window || !SetThreadDesktop(desktop)) return false;
  ShowWindow(search.window, SW_MAXIMIZE);
  bool success = waitMaximized(search.window, true);
  for (int shortcut = 0; shortcut < 2 && success; ++shortcut) {
    sendShortcut(search.window, shortcut == 1);
    success = waitFullscreen(search.window);
    if (success) {
      sendShortcut(search.window, shortcut == 1);
      success = waitMaximized(search.window, true);
    }
  }
  return SetThreadDesktop(previous) && success;
}

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
  const bool windowActions = argc == 4 && std::wstring(argv[3]) == L"--window-actions";
  if (argc == 4 && !windowActions) command += L" " + std::wstring(argv[3]);
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
  bool actionsOk = true;
  if (windowActions) {
    actionsOk = checkWindowActions(desktop, process.dwProcessId);
    std::printf("window actions %s\n", actionsOk ? "passed" : "failed"); std::fflush(stdout);
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
  return success && actionsOk ? 0 : 7;
}
