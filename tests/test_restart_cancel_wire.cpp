#include <fixture_restart/RestartWire.h>
#include <cstdlib>
using namespace FixtureRestart;
#define CHECK(x) do { if (!(x)) std::abort(); } while (false)

static void noArm()
{
  const CancelNoArm expected{1, 2, 4, 5, QByteArray(32, 'a')};
  const auto bytes = encodeCancelNoArm(expected); CHECK(!bytes.isEmpty());
  CHECK(decodeCancelNoArm(bytes, expected));
  for (qsizetype n = 0; n < bytes.size(); ++n) CHECK(!decodeCancelNoArm(bytes.left(n), expected));
  CHECK(!decodeCancelNoArm(bytes + 'x', expected));
  auto bad = bytes; bad[3] = '9'; CHECK(!decodeCancelNoArm(bad, expected));
  bad = bytes; bad[4] = 1; CHECK(!decodeCancelNoArm(bad, expected));
  auto wrong = expected; ++wrong.run; CHECK(!decodeCancelNoArm(bytes, wrong));
  wrong = expected; ++wrong.launch; CHECK(!decodeCancelNoArm(bytes, wrong));
  wrong = expected; ++wrong.revision; CHECK(!decodeCancelNoArm(bytes, wrong));
  wrong = expected; ++wrong.sequence; CHECK(!decodeCancelNoArm(bytes, wrong));
  wrong = expected; wrong.digest[0] ^= 1; CHECK(!decodeCancelNoArm(bytes, wrong));
}

static void armedResult()
{
  const CancelResult expected{1, 2, 3, 4, 5, 6, 7, 8, 9, CancelOutcome::Stopped, CancelFailure::None};
  const auto bytes = encodeCancelResult(expected); CHECK(!bytes.isEmpty());
  CancelResult result; CHECK(decodeCancelResult(bytes, expected, result) && result.outcome == CancelOutcome::Stopped);
  for (qsizetype n = 0; n < bytes.size(); ++n) {
    result.run = 99; CHECK(!decodeCancelResult(bytes.left(n), expected, result) && result.run == 99);
  }
  CHECK(!decodeCancelResult(bytes + 'x', expected, result));
  auto bad = bytes; bad[3] = '9'; CHECK(!decodeCancelResult(bad, expected, result));
  bad = bytes; bad[6] = 1; CHECK(!decodeCancelResult(bad, expected, result));
  auto wrong = expected; ++wrong.run; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.launch; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.grant; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.revision; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.sequence; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.host; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.core; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.intentRevision; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; ++wrong.attempt; CHECK(!decodeCancelResult(bytes, wrong, result));
  wrong = expected; wrong.outcome = CancelOutcome::Failed; CHECK(encodeCancelResult(wrong).isEmpty());
  wrong.reason = CancelFailure::CleanupUnconfirmed; CHECK(!encodeCancelResult(wrong).isEmpty());
}
int main() { noArm(); armedResult(); }
