#pragma once

#include "module_identity.h"
#include <memory>
#ifdef _WIN32
#include <windows.h>
#endif

namespace HealthyModuleIdentity {
// Observations alone are not launch authority or native retirement permission.
struct FileObservation {
  std::u16string canonicalPath;
  FileIdentity identity;
  std::uint64_t size = 0;
  Digest digest{};
  bool pathComplete = false, identityComplete = false, hashComplete = false;
};

enum class NativeError { None, Unsupported, Unavailable, InvalidDirectory, Open, Reparse, Path, Identity, Size, Read, Digest, Import, Module };

#ifdef _WIN32
struct StageApi {
  decltype(&CreateFileW) open = &CreateFileW;
  decltype(&CloseHandle) close = &CloseHandle;
  decltype(&GetFileInformationByHandleEx) information = &GetFileInformationByHandleEx;
  decltype(&GetFinalPathNameByHandleW) finalPath = &GetFinalPathNameByHandleW;
  decltype(&GetFileSizeEx) size = &GetFileSizeEx;
  decltype(&SetFilePointerEx) seek = &SetFilePointerEx;
  decltype(&ReadFile) read = &ReadFile;
#ifdef HEALTHY_MODULE_TESTING
  Digest (*overrideDigest)(const Digest&) = nullptr;
#endif
};
#endif

class StageLease {
public:
  // directoryHandle must be the parent-owned, independently verified fresh
  // staging directory. Caller retains this lease until child/job settlement.
  static std::unique_ptr<StageLease> acquire(std::uintptr_t directoryHandle, NativeError& error);
#if defined(_WIN32) && defined(HEALTHY_MODULE_TESTING)
  // Dedicated headless tests only. This cannot produce native qualification.
  static std::unique_ptr<StageLease> acquireForTest(std::uintptr_t directoryHandle, const StageApi& api, NativeError& error);
#endif
  ~StageLease();
  StageLease(const StageLease&) = delete;
  StageLease& operator=(const StageLease&) = delete;
  const FileObservation& observation() const noexcept;
private:
  struct State;
  explicit StageLease(std::unique_ptr<State> state);
#ifdef _WIN32
  static std::unique_ptr<StageLease> acquireImpl(std::uintptr_t directoryHandle, const StageApi& api, NativeError& error);
#endif
  std::unique_ptr<State> state_;
};

class ImportedModule {
public:
  // Reads the fixed render-update callback IAT slot used by the diagnostic
  // call site. It does not load a DLL or call an mpv function. Build/disassembly
  // evidence must still establish that the copied renderer uses this slot.
  static std::unique_ptr<ImportedModule> inspect(NativeError& error);
  ~ImportedModule();
  ImportedModule(const ImportedModule&) = delete;
  ImportedModule& operator=(const ImportedModule&) = delete;
  const FileObservation& observation() const noexcept;
  std::uintptr_t slot() const noexcept;
  std::uintptr_t firstTarget() const noexcept;
  std::uintptr_t secondTarget() const noexcept;
  std::uintptr_t module() const noexcept;
  std::uintptr_t executable() const noexcept;
  bool stillBound() const noexcept;
private:
  struct State;
  explicit ImportedModule(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}
