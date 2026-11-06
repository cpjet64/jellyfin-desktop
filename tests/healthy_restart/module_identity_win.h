#pragma once

#include "module_identity.h"
#include <memory>

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

class StageLease {
public:
  // directoryHandle must be the parent-owned, independently verified fresh
  // staging directory. Caller retains this lease until child/job settlement.
  static std::unique_ptr<StageLease> acquire(std::uintptr_t directoryHandle, NativeError& error);
  ~StageLease();
  StageLease(const StageLease&) = delete;
  StageLease& operator=(const StageLease&) = delete;
  const FileObservation& observation() const noexcept;
private:
  struct State;
  explicit StageLease(std::unique_ptr<State> state);
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
private:
  struct State;
  explicit ImportedModule(std::unique_ptr<State> state);
  std::unique_ptr<State> state_;
};
}
