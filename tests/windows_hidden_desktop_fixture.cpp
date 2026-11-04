#include <windows.h>
#include <cstdio>
#include <string>

int child(const wchar_t* eventName)
{
  HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName);
  if (!ready) return 21;
  const bool signaled = SetEvent(ready) != FALSE;
  CloseHandle(ready);
  if (!signaled) return 22;
  Sleep(90000);
  return 23;
}

int launchChild()
{
  const std::wstring name = L"Local\\JellyfinHelperFixture-" + std::to_wstring(GetCurrentProcessId());
  HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
  if (!ready) return 24;
  wchar_t path[MAX_PATH]{};
  if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) { CloseHandle(ready); return 25; }
  std::wstring command = L"\"" + std::wstring(path) + L"\" --child \"" + name + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(path, &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                      nullptr, nullptr, &startup, &process)) { CloseHandle(ready); return 26; }
  const DWORD wait = WaitForSingleObject(ready, 5000);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  CloseHandle(ready);
  if (wait != WAIT_OBJECT_0) return 27;
  std::puts("fixture child ready");
  std::fflush(stdout);
  return 0;
}

int wmain(int argc, wchar_t** argv)
{
  if (argc == 3 && std::wstring(argv[1]) == L"--child") return child(argv[2]);
  wchar_t mode[32]{};
  if (!GetEnvironmentVariableW(L"JELLYFIN_HELPER_FIXTURE_MODE", mode, 32)) return 20;
  const std::wstring selected(mode);
  if (selected == L"zero") return 0;
  if (selected == L"nonzero") return 42;
  if (selected == L"timeout") { Sleep(90000); return 28; }
  if (selected == L"child") return launchChild();
  return 29;
}
