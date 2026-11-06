#pragma once
#ifdef _WIN32
#include "module_identity_win.h"
#include <windows.h>
#include <string>

namespace HealthyModuleIdentity::NativeDetail {
struct FileHandle {
  HANDLE value = INVALID_HANDLE_VALUE;
  ~FileHandle();
};
bool canonical(HANDLE handle, std::wstring& output, const StageApi& api);
bool regularNonReparse(HANDLE handle, bool directory, const StageApi& api);
bool fileInfo(HANDLE handle, FileObservation& result, NativeError& error, const StageApi& api);
bool hashFile(HANDLE handle, FileObservation& result, NativeError& error, const StageApi& api);
}
#endif
