#include "healthy_restart/resume_progress.h"
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) std::abort(); } while (false)
using namespace HealthyRestart;
using namespace fixture_restart;

namespace {
constexpr std::int64_t start = 1000000000LL, step = 200000000LL;
constexpr std::int64_t dispatch = start + 15 * step;
void paint(FrameRecord& r, int x, int y, int width, int height, int value)
{
  for (int row = y; row < y + height; ++row) for (int col = x; col < x + width; ++col) {
    const auto p = std::size_t((row * r.width + col) * 4);
    r.rgba[p] = r.rgba[p + 1] = r.rgba[p + 2] = std::uint8_t(value);
  }
}
Capture frame(const FrameKey& key, int index, std::uint64_t serial, std::uint64_t time)
{
  auto r = std::make_shared<FrameRecord>();
  r->key = key; r->serial = serial; r->width = 160; r->height = 90;
  r->thread = 11; r->actualContext = 12; r->capturedAt = time;
  r->rgba.assign(160 * 90 * 4, 0);
  for (std::size_t i = 3; i < r->rgba.size(); i += 4) r->rgba[i] = 255;
  for (int bit = 0; bit < 9; ++bit) paint(*r, 5 + bit * 16, 8, 12, 12, index & (1 << bit) ? 220 : 70);
  paint(*r, (index * 3) % 140, 35, 20, 20, 220);
  return r;
}
PausedHoldBinding binding()
{
  PausedHoldBinding b;
  b.frame = {1, 2, 3, 4, 5, 6}; b.host = 7; b.revision = 8; b.attempt = 9; b.grant = 10;
  b.checkpointMs = 3000; b.deadlineNs = 20000000000LL;
  b.claim = std::make_shared<HealthyRetirement::RetirementClaim>(17);
  return b;
}
}

class TestPausedHold {
public:
  static std::unique_ptr<PausedHold> make(const PausedHoldBinding& b, std::shared_ptr<const PausedHoldProof>& proof)
  {
    std::unique_ptr<PausedHold> hold(new PausedHold(b));
    CHECK(hold->baseline(frame(b.frame, 0, 1, start - 1)) && hold->arm(1, start));
    hold->capture(frame(b.frame, 90, 2, start + 1));
    for (int i = 0; i <= 15; ++i) {
      const auto now = start + std::int64_t(i) * step;
      CHECK(hold->observe(std::uint64_t(i + 1), now, now, 3000));
    }
    proof = hold->finish(dispatch); CHECK(proof);
    return hold;
  }
};

class TestResumeProgress {
  static void observations(ResumeProgress& progress, int advanceMs = 1000)
  {
    CHECK(progress.initial(1, dispatch + 1, dispatch + 1, 3000));
    CHECK(progress.advance(2, dispatch + 1000000001LL, dispatch + 1000000001LL, 3000 + advanceMs));
  }
  static void frames(ResumeProgress& progress, const PausedHoldBinding& b)
  {
    progress.capture(frame(b.frame, 91, 3, dispatch + 1));
    progress.capture(frame(b.frame, 121, 4, dispatch + 1000000001LL));
  }
  static void goodAndLifetime()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> holdProof;
    auto hold = TestPausedHold::make(b, holdProof);
    ResumeProgress progress(b, holdProof, 2, dispatch);
    frames(progress, b); // Both captures can arrive before queued GUI observations.
    observations(progress);
    auto proof = progress.finish(dispatch + 1000000001LL); CHECK(proof);
    CHECK(proof->matches(b, dispatch + 1000000001LL));
    CHECK(!proof->matches(b, dispatch + 1000000000LL));
    auto wrong = b; ++wrong.grant; CHECK(!proof->matches(wrong, dispatch + 1000000001LL));
    progress.invalidate(); CHECK(!proof->matches(b, dispatch + 1000000001LL));
  }
  static void imageFailures()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
    ResumeProgress still(b, proof, 2, dispatch);
    still.capture(frame(b.frame, 91, 3, dispatch + 1));
    still.capture(frame(b.frame, 91, 4, dispatch + 1000000001LL));
    observations(still); CHECK(!still.finish(dispatch + 1000000001LL));
    ResumeProgress shortGap(b, proof, 2, dispatch);
    shortGap.capture(frame(b.frame, 91, 3, dispatch + 1));
    shortGap.capture(frame(b.frame, 121, 4, dispatch + 1000000000LL));
    observations(shortGap); CHECK(!shortGap.finish(dispatch + 1000000001LL));
    ResumeProgress wrongKey(b, proof, 2, dispatch);
    auto key = b.frame; ++key.entry;
    wrongKey.capture(frame(key, 91, 3, dispatch + 1));
    wrongKey.capture(frame(key, 121, 4, dispatch + 1000000001LL));
    observations(wrongKey); CHECK(!wrongKey.finish(dispatch + 1000000001LL));
    ResumeProgress old(b, proof, 2, dispatch);
    old.capture(frame(b.frame, 91, 3, dispatch - 1));
    old.capture(frame(b.frame, 121, 4, dispatch + 1000000001LL));
    observations(old); CHECK(!old.finish(dispatch + 1000000001LL));
  }
  static void observationFailures()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
    ResumeProgress shortAdvance(b, proof, 2, dispatch); frames(shortAdvance, b);
    CHECK(shortAdvance.initial(1, dispatch + 1, dispatch + 1, 3000));
    CHECK(!shortAdvance.advance(2, dispatch + 2, dispatch + 2, 3999));
    CHECK(!shortAdvance.finish(dispatch + 1000000001LL));
    ResumeProgress stale(b, proof, 2, dispatch);
    CHECK(!stale.initial(1, dispatch - 1, dispatch + 1, 3000));
    ResumeProgress duplicate(b, proof, 2, dispatch);
    CHECK(duplicate.initial(1, dispatch + 1, dispatch + 1, 3000));
    CHECK(!duplicate.advance(1, dispatch + 2, dispatch + 2, 4000));
    ResumeProgress expired(b, proof, 2, dispatch);
    CHECK(!expired.initial(1, dispatch + 10000000001LL, dispatch + 10000000001LL, 3000));
  }
  static void revokedAndDestroyed()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
    std::shared_ptr<const ResumeProgressProof> result;
    { ResumeProgress progress(b, proof, 2, dispatch); frames(progress, b); observations(progress);
      result = progress.finish(dispatch + 1000000001LL); CHECK(result); }
    CHECK(!result->matches(b, dispatch + 1000000001LL));
    ResumeProgress revoked(b, proof, 2, dispatch);
    std::const_pointer_cast<HealthyRetirement::RetirementClaim>(b.claim)->revoke();
    CHECK(!revoked.initial(1, dispatch + 1, dispatch + 1, 3000));
  }
  static void rejectBadCaptures()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
    for (int scenario = 0; scenario < 5; ++scenario) {
      ResumeProgress progress(b, proof, 2, dispatch);
      auto record = std::make_shared<FrameRecord>(*frame(b.frame, 91, 3, dispatch + 1));
      if (scenario == 0) ++record->thread;
      if (scenario == 1) ++record->actualContext;
      if (scenario == 2) record->serial = 2;
      if (scenario == 3) record->rgba.clear();
      if (scenario == 4) record->capturedAt = std::numeric_limits<std::uint64_t>::max();
      progress.capture(record);
      progress.capture(frame(b.frame, 121, 4, dispatch + 1000000001LL));
      observations(progress); CHECK(!progress.finish(dispatch + 1000000001LL));
    }
    ResumeProgress wrongBinding(b, proof, 2, dispatch);
    auto key = b.frame; ++key.phase;
    wrongBinding.capture(frame(key, 91, 3, dispatch + 1));
    wrongBinding.capture(frame(key, 121, 4, dispatch + 1000000001LL));
    observations(wrongBinding); CHECK(!wrongBinding.finish(dispatch + 1000000001LL));
  }
  static void holdRevocation()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
    ResumeProgress progress(b, proof, 2, dispatch); frames(progress, b); observations(progress);
    hold.reset(); CHECK(!progress.finish(dispatch + 1000000001LL));
    auto expired = binding(); expired.deadlineNs = dispatch + 1000000000LL;
    std::shared_ptr<const PausedHoldProof> expiredProof;
    auto expiredHold = TestPausedHold::make(expired, expiredProof);
    ResumeProgress deadline(expired, expiredProof, 2, dispatch);
    CHECK(!deadline.initial(1, expired.deadlineNs, expired.deadlineNs, 3000));
  }
  static void captureIsCopied()
  {
    auto b = binding(); std::shared_ptr<const PausedHoldProof> holdProof;
    auto hold = TestPausedHold::make(b, holdProof);
    ResumeProgress progress(b, holdProof, 2, dispatch);
    auto first = std::make_shared<FrameRecord>(*frame(b.frame, 91, 3, dispatch + 1));
    auto second = std::make_shared<FrameRecord>(*frame(b.frame, 121, 4, dispatch + 1000000001LL));
    progress.capture(first); first->rgba.clear();
    progress.capture(second); second->rgba.clear();
    observations(progress);
    const auto proof = progress.finish(dispatch + 1000000001LL);
    CHECK(proof && proof->matches(b, dispatch + 1000000001LL));
  }
public:
  static void run() { goodAndLifetime(); imageFailures(); observationFailures(); revokedAndDestroyed(); rejectBadCaptures(); holdRevocation(); captureIsCopied(); }
};
int main() { TestResumeProgress::run(); }
