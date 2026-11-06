#include "healthy_restart/module_gate.h"
#include <cstdlib>
using namespace HealthyModuleIdentity;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

static FileObservation fixture()
{
  FileObservation f;
  f.pathComplete = f.identityComplete = f.hashComplete = true;
  f.canonicalPath = u"C:\\fixture\\libmpv-2.dll";
  f.identity.volume = 123; f.identity.file[0] = 42;
  f.size = 9000; f.digest = expectedDigest();
  return f;
}

static bool sentinel(const LaunchIdentity& v)
{
  return v.launch == 77 && v.size == 88 && v.canonicalPath == u"unchanged";
}

static void rejectsWithoutOutputChange(const std::vector<std::uint8_t>& bytes)
{
  LaunchIdentity out; out.launch = 77; out.size = 88; out.canonicalPath = u"unchanged";
  CHECK(!decodeIdentity(bytes, out)); CHECK(sentinel(out));
}

static void framing()
{
  const auto f = fixture(); std::vector<std::uint8_t> bytes;
  CHECK(encodeIdentity(f, 12, bytes));
  LaunchIdentity result; CHECK(decodeIdentity(bytes, result));
  CHECK(result.launch == 12 && result.size == f.size && result.file.volume == f.identity.volume);
  CHECK(result.file.file == f.identity.file && result.digest == f.digest && result.canonicalPath == f.canonicalPath);
  for (std::size_t n = 0; n < bytes.size(); ++n)
    rejectsWithoutOutputChange(std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + n));
  auto bad = bytes; bad.push_back(0); rejectsWithoutOutputChange(bad);
  bad = bytes; bad[0] ^= 1; rejectsWithoutOutputChange(bad);
  bad = bytes; bad[4] = 0; rejectsWithoutOutputChange(bad);
  bad = bytes; bad[20] = 0; rejectsWithoutOutputChange(bad);
  bad = bytes; bad[28] = 0; rejectsWithoutOutputChange(bad);
  bad = bytes; bad[44] ^= 1; rejectsWithoutOutputChange(bad);
  bad = bytes; bad[76] = 0; bad[77] = 0; rejectsWithoutOutputChange(bad);
  bad = bytes; bad.resize(MaxIdentityFrame + 1); rejectsWithoutOutputChange(bad);
}

static void encoderRejectsInvalid()
{
  auto f = fixture(); std::vector<std::uint8_t> out{7, 8};
  CHECK(!encodeIdentity(f, 0, out) && out == std::vector<std::uint8_t>({7, 8}));
  f.pathComplete = false; CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.identityComplete = false; CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.hashComplete = false; CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.digest[0] ^= 1; CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.identity.file = {}; CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.canonicalPath.assign(1025, u'x'); CHECK(!encodeIdentity(f, 12, out));
  f = fixture(); f.canonicalPath.push_back(u'\0'); CHECK(!encodeIdentity(f, 12, out));
  CHECK(out == std::vector<std::uint8_t>({7, 8}));
}

int main() { framing(); encoderRejectsInvalid(); }
