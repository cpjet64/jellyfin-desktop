#include "healthy_restart/module_identity.h"
#include <cstdlib>
using namespace HealthyModuleIdentity;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
static const Digest selected = {{
  0x5c,0x5c,0x9d,0x09,0x48,0x59,0xbd,0xc6,0x0b,0x8b,0x27,0xb2,0xd8,0x32,0xdb,0x8f,
  0xa8,0xc2,0xe4,0x87,0xef,0xbd,0x35,0xf5,0xae,0xa5,0xaf,0xe6,0x96,0x0d,0xad,0x39
}};

// Synthetic observations exercise comparison only and cannot authorize native use.
static Evidence validFacts()
{
  Evidence e;
  e.platformAvailable = e.parentLeaseHeld = e.channelBound = true;
  e.modulePathComplete = e.parentHashComplete = e.childHashComplete = true;
  e.parentIdentityComplete = e.childIdentityComplete = e.moduleReferenceHeld = true;
  e.expectedLaunch = e.observedLaunch = 7;
  e.expectedSize = e.observedSize = 123456;
  e.expectedFile.volume = e.observedFile.volume = 17;
  e.expectedFile.file[0] = e.observedFile.file[0] = 42;
  e.expectedPath = e.observedPath = u"C:\\owned\\libmpv-2.dll";
  e.parentDigest = e.childDigest = selected;
  e.callSiteSlot = 0x10; e.firstTarget = e.secondTarget = 0x20;
  e.targetModule = 0x30; e.executableModule = 0x40;
  return e;
}

static void authorityAndBinding()
{
  CHECK(compare(Evidence{}) == Rejection::Unavailable);
  auto e = validFacts(); CHECK(compare(e) == Rejection::None);
  e.platformAvailable = false; CHECK(compare(e) == Rejection::Unavailable);
  e = validFacts(); e.parentLeaseHeld = false; CHECK(compare(e) == Rejection::ParentLease);
  e = validFacts(); e.channelBound = false; CHECK(compare(e) == Rejection::Channel);
  e = validFacts(); ++e.observedLaunch; CHECK(compare(e) == Rejection::Launch);
  e = validFacts(); e.expectedLaunch = e.observedLaunch = 0; CHECK(compare(e) == Rejection::Launch);
  e = validFacts(); e.callSiteSlot = 0; CHECK(compare(e) == Rejection::Binding);
  e = validFacts(); e.firstTarget = 0; CHECK(compare(e) == Rejection::Binding);
  e = validFacts(); ++e.secondTarget; CHECK(compare(e) == Rejection::Binding);
  e = validFacts(); e.targetModule = e.executableModule; CHECK(compare(e) == Rejection::Module);
  e = validFacts(); e.targetModule = 0; CHECK(compare(e) == Rejection::Module);
  e = validFacts(); e.moduleReferenceHeld = false; CHECK(compare(e) == Rejection::Module);
}

static void fileAndHash()
{
  auto e = validFacts(); e.modulePathComplete = false; CHECK(compare(e) == Rejection::Path);
  e = validFacts(); e.observedPath += u"x"; CHECK(compare(e) == Rejection::Path);
  e = validFacts(); e.expectedPath.clear(); CHECK(compare(e) == Rejection::Path);
  e = validFacts(); e.expectedPath.assign(32768, u'x'); e.observedPath = e.expectedPath;
  CHECK(compare(e) == Rejection::Path);
  e = validFacts(); e.parentIdentityComplete = false; CHECK(compare(e) == Rejection::FileIdentity);
  e = validFacts(); e.childIdentityComplete = false; CHECK(compare(e) == Rejection::FileIdentity);
  e = validFacts(); ++e.observedFile.volume; CHECK(compare(e) == Rejection::FileIdentity);
  for (std::size_t i = 0; i < e.observedFile.file.size(); ++i) {
    e = validFacts(); e.observedFile.file[i] ^= 1; CHECK(compare(e) == Rejection::FileIdentity);
  }
  e = validFacts(); e.expectedFile.file = {}; e.observedFile.file = {};
  CHECK(compare(e) == Rejection::FileIdentity);
  e = validFacts(); ++e.observedSize; CHECK(compare(e) == Rejection::FileSize);
  e = validFacts(); e.parentHashComplete = false; CHECK(compare(e) == Rejection::HashIncomplete);
  e = validFacts(); e.childHashComplete = false; CHECK(compare(e) == Rejection::HashIncomplete);
  for (std::size_t i = 0; i < selected.size(); ++i) {
    e = validFacts(); e.parentDigest[i] ^= 1; CHECK(compare(e) == Rejection::Digest);
    e = validFacts(); e.childDigest[i] ^= 1; CHECK(compare(e) == Rejection::Digest);
  }
}

int main()
{
  authorityAndBinding();
  fileAndHash();
}
