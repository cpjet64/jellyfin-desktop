#include <fixture_restart/RestartWire.h>
#include <cstdlib>
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

static void closedFrames()
{
  const Closed expected{1, 2, 3, 4, 5, 6, 7};
  const QByteArray bytes = encodeClosed(expected); CHECK(!bytes.isEmpty());
  Closed result; CHECK(decodeClosed(bytes, expected, result) && result.closure == 7);
  for (qsizetype n = 0; n < bytes.size(); ++n) {
    result.closure = 99; CHECK(!decodeClosed(bytes.left(n), expected, result)); CHECK(result.closure == 99);
  }
  CHECK(!decodeClosed(bytes + 'x', expected, result));
  auto bad = bytes; bad[3] = '9'; CHECK(!decodeClosed(bad, expected, result));
  auto wrong = expected; ++wrong.run; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.launch; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.stopSequence; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.revision; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.core; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.renderer; CHECK(!decodeClosed(bytes, wrong, result));
  wrong = expected; ++wrong.closure; CHECK(!decodeClosed(bytes, wrong, result));
}

static void acceptedFrames()
{
  const GrantAccepted expected{1, 2, 3, 4, QByteArray(32, 'a'), 5, 6, 7, 8};
  const QByteArray bytes = encodeGrantAccepted(expected); CHECK(!bytes.isEmpty());
  GrantAccepted result; CHECK(decodeGrantAccepted(bytes, expected, result) && result.digest == expected.digest);
  CHECK(result.host == 5 && result.core == 6 && result.intentRevision == 7 && result.attempt == 8);
  for (qsizetype n = 0; n < bytes.size(); ++n) {
    result.run = 99; CHECK(!decodeGrantAccepted(bytes.left(n), expected, result)); CHECK(result.run == 99);
  }
  CHECK(!decodeGrantAccepted(bytes + 'x', expected, result));
  auto bad = bytes; bad[3] = '9'; CHECK(!decodeGrantAccepted(bad, expected, result));
  auto wrong = expected; ++wrong.run; CHECK(!decodeGrantAccepted(bytes, wrong, result));
  wrong = expected; ++wrong.launch; CHECK(!decodeGrantAccepted(bytes, wrong, result));
  wrong = expected; ++wrong.grant; CHECK(!decodeGrantAccepted(bytes, wrong, result));
  wrong = expected; ++wrong.revision; CHECK(!decodeGrantAccepted(bytes, wrong, result));
  wrong = expected; wrong.digest[0] ^= 1; CHECK(!decodeGrantAccepted(bytes, wrong, result));
  wrong = expected; wrong.digest.clear(); CHECK(encodeGrantAccepted(wrong).isEmpty());
  wrong = expected; wrong.host = 0; CHECK(encodeGrantAccepted(wrong).isEmpty());
  wrong = expected; wrong.core = 0; CHECK(encodeGrantAccepted(wrong).isEmpty());
  wrong = expected; wrong.intentRevision = 0; CHECK(encodeGrantAccepted(wrong).isEmpty());
  wrong = expected; wrong.attempt = 0; CHECK(encodeGrantAccepted(wrong).isEmpty());
}
static void permitFrames()
{
  const RestorePermit expected{1, 2, 3, 4, 5, QByteArray(32, 'a')};
  const QByteArray bytes = encodeRestorePermit(expected); CHECK(!bytes.isEmpty());
  RestorePermit result; CHECK(decodeRestorePermit(bytes, expected, result) && result.sequence == 5);
  for (qsizetype n = 0; n < bytes.size(); ++n) {
    result.sequence = 99; CHECK(!decodeRestorePermit(bytes.left(n), expected, result)); CHECK(result.sequence == 99);
  }
  CHECK(!decodeRestorePermit(bytes + 'x', expected, result));
  auto bad = bytes; bad[3] = '9'; CHECK(!decodeRestorePermit(bad, expected, result));
  auto wrong = expected; ++wrong.run; CHECK(!decodeRestorePermit(bytes, wrong, result));
  wrong = expected; ++wrong.launch; CHECK(!decodeRestorePermit(bytes, wrong, result));
  wrong = expected; ++wrong.grant; CHECK(!decodeRestorePermit(bytes, wrong, result));
  wrong = expected; ++wrong.revision; CHECK(!decodeRestorePermit(bytes, wrong, result));
  wrong = expected; ++wrong.sequence; CHECK(!decodeRestorePermit(bytes, wrong, result));
  wrong = expected; wrong.digest[0] ^= 1; CHECK(!decodeRestorePermit(bytes, wrong, result));
}
int main() { closedFrames(); acceptedFrames(); permitFrames(); }
