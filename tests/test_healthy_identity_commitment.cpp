#include <healthy_restart/module_gate.h>
#include <cstdlib>
using namespace HealthyModuleIdentity;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

static FileObservation file()
{
  FileObservation f; f.pathComplete = f.identityComplete = f.hashComplete = true;
  f.canonicalPath = u"C:\\fixture\\libmpv-2.dll"; f.identity.volume = 7;
  f.identity.file[0] = 8; f.size = 9; f.digest = expectedDigest(); return f;
}
static LaunchBinding source()
{
  LaunchBinding b; b.role = LaunchRole::Source; b.run = 12; b.revision = 13;
  b.assetDigest[0] = 14; return b;
}
static LaunchBinding restore()
{
  LaunchBinding b; b.role = LaunchRole::Restore; b.run = 12;
  b.expectedGrantDigest[0] = 15; return b;
}
static void roundtrip(LaunchBinding binding)
{
  std::vector<std::uint8_t> bytes; CHECK(encodeIdentity(file(), 22, binding, bytes));
  LaunchIdentity decoded; CHECK(decodeIdentity(bytes, decoded));
  CHECK(decoded.launch == 22 && decoded.binding.role == binding.role && decoded.binding.run == binding.run);
  CHECK(decoded.binding.revision == binding.revision && decoded.binding.assetDigest == binding.assetDigest);
  CHECK(decoded.binding.expectedGrantDigest == binding.expectedGrantDigest);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    LaunchIdentity untouched; untouched.launch = 99;
    CHECK(!decodeIdentity(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + i), untouched));
    CHECK(untouched.launch == 99);
  }
  auto bad = bytes; bad.push_back(0); CHECK(!decodeIdentity(bad, decoded));
  bad = bytes; bad[3] = '1'; CHECK(!decodeIdentity(bad, decoded));
  bad = bytes; bad[3] = '9'; CHECK(!decodeIdentity(bad, decoded));
  bad = bytes; bad[76] = 0; CHECK(!decodeIdentity(bad, decoded));
  bad = bytes; bad.resize(MaxIdentityFrame + 1); CHECK(!decodeIdentity(bad, decoded));
  std::vector<std::uint8_t> framed; CHECK(frameIdentity(file(), 22, binding, framed));
  CHECK(framed.size() == bytes.size() + 4 && framed[0] == 0 && framed[1] == 0);
  CHECK((unsigned(framed[2]) << 8 | framed[3]) == bytes.size());
}
static void invalidBinding()
{
  std::vector<std::uint8_t> out{9}; auto b = source();
  b.run = 0; CHECK(!encodeIdentity(file(), 22, b, out));
  b = source(); b.revision = 0; CHECK(!encodeIdentity(file(), 22, b, out));
  b = source(); b.assetDigest = {}; CHECK(!encodeIdentity(file(), 22, b, out));
  b = source(); b.expectedGrantDigest[0] = 1; CHECK(!encodeIdentity(file(), 22, b, out));
  b = restore(); b.expectedGrantDigest = {}; CHECK(!encodeIdentity(file(), 22, b, out));
  b = restore(); b.assetDigest[0] = 1; CHECK(!encodeIdentity(file(), 22, b, out));
  b = restore(); b.revision = 1; CHECK(!encodeIdentity(file(), 22, b, out));
  b = restore(); b.role = static_cast<LaunchRole>(99); CHECK(!encodeIdentity(file(), 22, b, out));
  CHECK(!encodeIdentity(file(), 0, restore(), out)); CHECK(out == std::vector<std::uint8_t>({9}));
}
int main() { roundtrip(source()); roundtrip(restore()); invalidBinding(); }
