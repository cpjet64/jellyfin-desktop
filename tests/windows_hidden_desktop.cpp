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

bool sendShortcut(HWND window, bool alt)
{
  const DWORD targetThread = GetWindowThreadProcessId(window, nullptr);
  const DWORD ownThread = GetCurrentThreadId();
  const bool attached = AttachThreadInput(ownThread, targetThread, TRUE) != FALSE;
  if (!attached) {
    std::printf("window actions attach failed %lu\n", GetLastError()); std::fflush(stdout);
    return false;
  }
  SetFocus(window);
  BYTE state[256]{};
  if (attached && alt) {
    GetKeyboardState(state);
    state[VK_MENU] |= 0x80;
    state[VK_LMENU] |= 0x80;
    SetKeyboardState(state);
    PostMessageW(window, WM_SYSKEYDOWN, VK_MENU, 0x20380001);
    Sleep(100);
  }
  const WPARAM key = alt ? VK_RETURN : VK_F11;
  PostMessageW(window, alt ? WM_SYSKEYDOWN : WM_KEYDOWN, key, alt ? 0x20000001 : 1);
  Sleep(250);
  PostMessageW(window, alt ? WM_SYSKEYUP : WM_KEYUP, key, alt ? 0xE0000001 : 0xC0000001);
  Sleep(250);
  if (attached && alt) {
    PostMessageW(window, WM_SYSKEYUP, VK_MENU, 0xE0380001);
    state[VK_MENU] &= 0x7f;
    state[VK_LMENU] &= 0x7f;
    SetKeyboardState(state);
  }
  AttachThreadInput(ownThread, targetThread, FALSE);
  return true;
}

bool checkNormalShortcuts(HWND window)
{
  ShowWindow(window, SW_RESTORE);
  if (!SetWindowPos(window, nullptr, 120, 120, 960, 680, SWP_NOZORDER | SWP_NOACTIVATE)) return false;
  Sleep(500);
  RECT original{};
  if (!GetWindowRect(window, &original)) return false;
  for (int shortcut = 0; shortcut < 2; ++shortcut) {
    if (!sendShortcut(window, shortcut == 1) || !waitFullscreen(window) ||
        !sendShortcut(window, shortcut == 1)) return false;
    bool restored = false;
    for (int i = 0; i < 50; ++i) {
      RECT current{};
      if (GetWindowRect(window, &current) && EqualRect(&original, &current)) { restored = true; break; }
      Sleep(100);
    }
    if (!restored) return false;
  }
  return true;
}

bool checkWindowActions(HDESK desktop, DWORD pid, bool geometryOnly)
{
  WindowSearch search{pid, nullptr};
  for (int i = 0; i < 100 && !search.window; ++i) {
    EnumDesktopWindows(desktop, findWindow, reinterpret_cast<LPARAM>(&search));
    if (!search.window) Sleep(100);
  }
  HDESK previous = GetThreadDesktop(GetCurrentThreadId());
  if (!search.window) { std::puts("window actions window not found"); return false; }
  if (!SetThreadDesktop(desktop)) {
    std::printf("window actions desktop failed %lu\n", GetLastError()); return false;
  }
  if (geometryOnly) {
    RECT rect{};
    bool ok = GetWindowRect(search.window, &rect) != FALSE;
    if (ok) std::printf("window bounds %ld %ld %ld %ld\n", rect.left, rect.top, rect.right, rect.bottom);
    std::fflush(stdout);
    return SetThreadDesktop(previous) && ok;
  }
  ShowWindow(search.window, SW_MAXIMIZE);
  bool success = waitMaximized(search.window, true);
  if (!success) std::puts("window actions maximize failed");
  for (int shortcut = 0; shortcut < 2 && success; ++shortcut) {
    const char* name = shortcut ? "Alt+Return" : "F11";
    success = sendShortcut(search.window, shortcut == 1) && waitFullscreen(search.window);
    std::printf("window actions %s enter %s\n", name, success ? "passed" : "failed");
    if (success) {
      success = sendShortcut(search.window, shortcut == 1) && waitMaximized(search.window, true);
      std::printf("window actions %s restore %s\n", name, success ? "passed" : "failed");
    }
  }
  if (success) {
    success = checkNormalShortcuts(search.window);
    std::printf("window actions normal bounds %s\n", success ? "passed" : "failed");
    RECT rect{};
    if (success && GetWindowRect(search.window, &rect))
      std::printf("window bounds %ld %ld %ld %ld\n", rect.left, rect.top, rect.right, rect.bottom);
    else success = false;
  }
  std::fflush(stdout);
  return SetThreadDesktop(previous) && success;
}

bool waitForReady()
{
  HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
  for (int i = 0; i < 300; ++i) {
    DWORD available = 0;
    if (!PeekNamedPipe(input, nullptr, 0, nullptr, &available, nullptr)) return false;
    if (available) {
      char byte;
      DWORD read = 0;
      return ReadFile(input, &byte, 1, &read, nullptr) && read == 1;
    }
    Sleep(100);
  }
  return false;
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
  std::printf("created desktop JellyfinSmoke-%lu\n", GetCurrentProcessId()); std::fflush(stdout);
  HANDLE job = CreateJobObjectW(nullptr, nullptr);
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  if (!job || !SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits))) {
    std::fprintf(stderr, "Job setup failed: %lu\n", GetLastError());
    if (job) CloseHandle(job);
    CloseDesktop(desktop);
    return 4;
  }
  const bool glProbe = argc == 4 && std::wstring(argv[3]) == L"--gl-probe";
  const bool testExecutable = glProbe || (argc == 4 && std::wstring(argv[3]) == L"--test-executable");
  std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
  if (!testExecutable) command += L" --config-dir \"" + std::wstring(argv[2]) + L"\"";
  if (glProbe) command += L" \"" + std::wstring(argv[2]) + L"\\gl-probe.json\"";
  else if (testExecutable) command += L" -o \"" + std::wstring(argv[2]) + L"\\qtest.txt,txt\"";
  const bool geometryOnly = argc == 4 && std::wstring(argv[3]) == L"--geometry-observe";
  const bool windowActions = argc == 4 && (std::wstring(argv[3]) == L"--window-actions" || geometryOnly);
  if (argc == 4 && !windowActions && !testExecutable) command += L" " + std::wstring(argv[3]);
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.lpDesktop = const_cast<wchar_t *>(name.c_str());
  startup.dwFlags = STARTF_USESTDHANDLES;
  startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
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
    actionsOk = waitForReady() && checkWindowActions(desktop, process.dwProcessId, geometryOnly);
    std::printf("window actions %s\n", actionsOk ? "passed" : "failed"); std::fflush(stdout);
  }
  const DWORD wait = WaitForSingleObject(process.hProcess, testExecutable ? 60000 : 12000);
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
