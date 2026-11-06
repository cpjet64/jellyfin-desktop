#pragma once

#include "module_identity_win.h"
#include <vector>

namespace HealthyModuleIdentity {
enum class LaunchRole : std::uint8_t { Source = 1, Restore = 2 };
struct LaunchBinding {
  LaunchRole role = LaunchRole::Source;
  std::uint64_t run = 0, revision = 0;
  Digest assetDigest{}, expectedGrantDigest{};
};
// Decoded bytes are data only: they do not establish the sender, a retained
// parent lease, an actual call-site binding, or retirement permission.
struct LaunchIdentity {
  std::uint64_t launch = 0, size = 0;
  FileIdentity file;
  Digest digest{};
  std::u16string canonicalPath;
  LaunchBinding binding;
};

constexpr std::size_t MaxIdentityFrame = 4096;
bool encodeIdentity(const FileObservation& observation, std::uint64_t launch, const LaunchBinding& binding,
                    std::vector<std::uint8_t>& output);
bool frameIdentity(const FileObservation& observation, std::uint64_t launch, const LaunchBinding& binding,
                   std::vector<std::uint8_t>& output);
bool decodeIdentity(const std::vector<std::uint8_t>& input, LaunchIdentity& output);
}
