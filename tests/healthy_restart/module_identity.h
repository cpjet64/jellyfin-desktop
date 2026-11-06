#pragma once

#include <array>
#include <cstdint>
#include <string>

namespace HealthyModuleIdentity {
using Digest = std::array<std::uint8_t, 32>;
struct FileIdentity {
  std::uint64_t volume = 0;
  std::array<std::uint8_t, 16> file{};
};

// Injected observations can exercise this policy, but cannot establish parent
// channel authority or produce permission to use the native callback barrier.
struct Evidence {
  bool platformAvailable = false, parentLeaseHeld = false, channelBound = false;
  bool modulePathComplete = false, parentHashComplete = false, childHashComplete = false;
  bool parentIdentityComplete = false, childIdentityComplete = false, moduleReferenceHeld = false;
  std::uint64_t expectedLaunch = 0, observedLaunch = 0;
  std::uint64_t expectedSize = 0, observedSize = 0;
  FileIdentity expectedFile, observedFile;
  std::u16string expectedPath, observedPath;
  Digest parentDigest{}, childDigest{};
  std::uintptr_t callSiteSlot = 0, firstTarget = 0, secondTarget = 0;
  std::uintptr_t targetModule = 0, executableModule = 0;
};

enum class Rejection {
  None, Unavailable, ParentLease, Channel, Launch, Binding, Module, Path,
  FileIdentity, FileSize, HashIncomplete, Digest
};

const Digest& expectedDigest() noexcept;
Rejection compare(const Evidence& evidence) noexcept;
}
