#include "module_identity.h"

namespace HealthyModuleIdentity {
const Digest& expectedDigest() noexcept
{
  static const Digest value{{0x5c,0x5c,0x9d,0x09,0x48,0x59,0xbd,0xc6,0x0b,0x8b,0x27,0xb2,0xd8,0x32,0xdb,0x8f,
                             0xa8,0xc2,0xe4,0x87,0xef,0xbd,0x35,0xf5,0xae,0xa5,0xaf,0xe6,0x96,0x0d,0xad,0x39}};
  return value;
}

Rejection compare(const Evidence& value) noexcept
{
  if (!value.platformAvailable) return Rejection::Unavailable;
  if (!value.parentLeaseHeld) return Rejection::ParentLease;
  if (!value.channelBound) return Rejection::Channel;
  if (!value.expectedLaunch || value.expectedLaunch != value.observedLaunch) return Rejection::Launch;
  if (!value.callSiteSlot || !value.firstTarget || value.firstTarget != value.secondTarget) return Rejection::Binding;
  if (!value.targetModule || !value.executableModule || value.targetModule == value.executableModule ||
      !value.moduleReferenceHeld) return Rejection::Module;
  if (!value.modulePathComplete || value.expectedPath.empty() || value.expectedPath.size() > 32767 ||
      value.expectedPath.find(u'\0') != std::u16string::npos || value.expectedPath != value.observedPath) return Rejection::Path;
  if (!value.parentIdentityComplete || !value.childIdentityComplete || !value.expectedFile.volume ||
      value.expectedFile.volume != value.observedFile.volume || value.expectedFile.file != value.observedFile.file ||
      value.expectedFile.file == std::array<std::uint8_t, 16>{}) return Rejection::FileIdentity;
  if (!value.expectedSize || value.expectedSize != value.observedSize) return Rejection::FileSize;
  if (!value.parentHashComplete || !value.childHashComplete) return Rejection::HashIncomplete;
  if (value.parentDigest != expectedDigest() || value.childDigest != expectedDigest()) return Rejection::Digest;
  return Rejection::None;
}
}
