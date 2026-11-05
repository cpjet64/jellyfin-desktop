#include <windows.h>
#include <delayimp.h>
#include <intrin.h>
#include <cstring>
#include <string>
#include "WindowsLoaderMode.h"
#ifdef JELLYFIN_LOADER_TEST_V3
#include <cstdio>
#define LOADER_TRACE(...) do { std::fprintf(stderr, __VA_ARGS__); std::fflush(stderr); } while (false)
#else
#define LOADER_TRACE(...) do {} while (false)
#endif

namespace {

struct LoaderApi
{
  using Handle = HMODULE;
  DWORD mode() const noexcept
  {
    DWORD value = GetThreadErrorMode();
    LOADER_TRACE("mode before=%lu requested=%lu\n", value, value | SEM_FAILCRITICALERRORS);
    return value;
  }
  DWORD failCriticalErrors() const noexcept { return SEM_FAILCRITICALERRORS; }
  DWORD lastError() const noexcept { return GetLastError(); }
  bool setMode(DWORD value, DWORD *previous) const noexcept
  {
    BOOL ok = SetThreadErrorMode(value, previous);
    DWORD error = ok ? ERROR_SUCCESS : GetLastError();
    LOADER_TRACE("mode %s requested=%lu ok=%d previous=%lu error=%lu observed=%lu\n",
                 previous ? "enter" : "restore", value, ok, previous && ok ? *previous : 0,
                 error, GetThreadErrorMode());
    if (!ok) SetLastError(error);
    return ok != FALSE;
  }
  HMODULE load(const wchar_t *path) const noexcept
  {
    LOADER_TRACE("before load mode=%lu\n", GetThreadErrorMode());
    HMODULE module = LoadLibraryExW(path, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                                    LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    DWORD error = GetLastError();
    LOADER_TRACE("after load loaded=%d error=%lu\n", module != nullptr, error);
    SetLastError(error);
    return module;
  }
};

void loaderFailure(const wchar_t *phase, bool loaded, DWORD pathError, DWORD entryError,
                   DWORD loadError, DWORD restoreError, DWORD primary) noexcept
{
  wchar_t diagnostic[224];
  _snwprintf_s(diagnostic, _countof(diagnostic), _TRUNCATE,
               L"Jellyfin MPV loader: phase=%ls loaded=%d path_error=%lu entry_error=%lu "
               L"load_error=%lu restore_error=%lu\n", phase, loaded, pathError, entryError,
               loadError, restoreError);
  OutputDebugStringW(diagnostic);
  SetLastError(primary);
}

bool supportsX64V3()
{
#if defined(JELLYFIN_LOADER_TEST_V3)
  return JELLYFIN_LOADER_TEST_V3 != 0;
#elif defined(_M_X64)
  int r[4];
  __cpuid(r, 0);
  if (r[0] < 7) return false;
  __cpuid(r, 1);
  const unsigned ecx = static_cast<unsigned>(r[2]);
  // x86-64-v2 and v3 CPU features, including OS-enabled AVX state.
  constexpr unsigned required = (1u << 0) | (1u << 9) | (1u << 12) |
      (1u << 13) | (1u << 19) | (1u << 20) | (1u << 22) | (1u << 23) |
      (1u << 26) | (1u << 27) | (1u << 28) | (1u << 29);
  if ((ecx & required) != required || (_xgetbv(0) & 6) != 6) return false;
  __cpuidex(r, 7, 0);
  constexpr unsigned extended = (1u << 3) | (1u << 5) | (1u << 8);
  if ((static_cast<unsigned>(r[1]) & extended) != extended) return false;
  __cpuid(r, static_cast<int>(0x80000000u));
  if (static_cast<unsigned>(r[0]) < 0x80000001u) return false;
  __cpuid(r, static_cast<int>(0x80000001u));
  return (static_cast<unsigned>(r[2]) & ((1u << 0) | (1u << 5))) ==
      ((1u << 0) | (1u << 5));
#else
  return true;
#endif
}

HMODULE loadMpv(bool primary)
{
  wchar_t path[32768];
  const DWORD capacity = static_cast<DWORD>(sizeof(path) / sizeof(path[0]));
  const DWORD size = GetModuleFileNameW(nullptr, path, capacity);
  if (!size) {
    const DWORD error = GetLastError();
    loaderFailure(L"path", false, error, 0, 0, 0, error);
    return nullptr;
  }
  if (size >= capacity) {
    loaderFailure(L"path", false, ERROR_INSUFFICIENT_BUFFER, 0, 0, 0, ERROR_INSUFFICIENT_BUFFER);
    return nullptr;
  }
  std::wstring directory(path, size);
  const auto slash = directory.find_last_of(L"\\/");
  if (slash == std::wstring::npos) {
    loaderFailure(L"path", false, ERROR_BAD_PATHNAME, 0, 0, 0, ERROR_BAD_PATHNAME);
    return nullptr;
  }
  directory.resize(slash + 1);
  directory += primary ? L"libmpv-2.dll" : L"libmpv-fallback.dll";
  LoaderApi api;
  const auto result = loadWithThreadErrorMode(api, directory.c_str());
  if (!result.entered || !result.module || !result.restored) {
    const DWORD failure = result.error ? result.error : result.restoreError;
    const wchar_t *phase = !result.entered ? L"mode_entry" : !result.restored ? L"mode_restore" : L"load";
    loaderFailure(phase, result.module != nullptr, 0, result.entered ? 0ul : result.error,
                  result.entered ? result.error : 0ul, result.restoreError, failure);
    return nullptr;
  }
  return result.module;
}

HMODULE selectMpv(bool supportsV3, HMODULE (*load)(bool))
{
  return load(supportsV3);
}

FARPROC WINAPI mpvDelayHook(unsigned notification, PDelayLoadInfo info)
{
  if (notification != dliNotePreLoadLibrary ||
      _stricmp(info->szDll, "libmpv-2.dll") != 0) return nullptr;
  const bool supportsV3 = supportsX64V3();
  LOADER_TRACE("delay hook variant=%s\n", supportsV3 ? "primary" : "fallback");
  HMODULE module = selectMpv(supportsV3, loadMpv);
  if (!module) {
    const DWORD failure = GetLastError();
#ifndef JELLYFIN_LOADER_TEST_V3
    MessageBoxW(nullptr, supportsV3 ? L"libmpv-2.dll could not be loaded. Reinstall Jellyfin Desktop."
                                  : L"libmpv-fallback.dll could not be loaded. Reinstall Jellyfin Desktop.",
                L"Jellyfin Desktop", MB_OK | MB_ICONERROR);
#endif
    LOADER_TRACE("before ExitProcess policy=%lu loader_error=%lu\n", ERROR_MOD_NOT_FOUND, failure);
    SetLastError(failure);
    ExitProcess(ERROR_MOD_NOT_FOUND);
  }
  return reinterpret_cast<FARPROC>(module);
}

FARPROC WINAPI mpvFailureHook(unsigned notification, PDelayLoadInfo info)
{
  if (notification != dliFailGetProc || _stricmp(info->szDll, "libmpv-2.dll") != 0)
    return nullptr;
#ifndef JELLYFIN_LOADER_TEST_V3
  MessageBoxW(nullptr, L"The installed libmpv is incompatible. Reinstall Jellyfin Desktop.",
              L"Jellyfin Desktop", MB_OK | MB_ICONERROR);
#endif
  LOADER_TRACE("missing export hook\nbefore ExitProcess policy=%lu\n", ERROR_PROC_NOT_FOUND);
  ExitProcess(ERROR_PROC_NOT_FOUND);
}
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = mpvDelayHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = mpvFailureHook;
