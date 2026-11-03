#include <windows.h>
#include <delayimp.h>
#include <intrin.h>
#include <cstring>
#include <string>

namespace {

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
  if (!size || size >= capacity) return nullptr;
  std::wstring directory(path, size);
  const auto slash = directory.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return nullptr;
  directory.resize(slash + 1);
  directory += primary ? L"libmpv-2.dll" : L"libmpv-fallback.dll";
  return LoadLibraryExW(directory.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR |
                        LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
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
  HMODULE module = selectMpv(supportsV3, loadMpv);
  if (!module) {
#ifndef JELLYFIN_LOADER_TEST_V3
    MessageBoxW(nullptr, supportsV3 ? L"libmpv-2.dll could not be loaded. Reinstall Jellyfin Desktop."
                                  : L"libmpv-fallback.dll could not be loaded. Reinstall Jellyfin Desktop.",
                L"Jellyfin Desktop", MB_OK | MB_ICONERROR);
#endif
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
  ExitProcess(ERROR_PROC_NOT_FOUND);
}
}

extern "C" const PfnDliHook __pfnDliNotifyHook2 = mpvDelayHook;
extern "C" const PfnDliHook __pfnDliFailureHook2 = mpvFailureHook;
