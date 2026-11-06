#include "healthy_restart/resume_progress.h"
#include "healthy_restart/capture_capability.h"
#include <algorithm>
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <limits>

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__, #x); std::abort(); } } while (false)
using namespace HealthyRestart;
using namespace fixture_restart;

class TestCaptureCapability {
  using Bridge = HealthyRetirement::CaptureBridge;
  using Identity = HealthyRetirement::Identity;
  using Fbo = HealthyRetirement::FramebufferObservation;
  struct Issuer {
    std::shared_ptr<HealthyRetirement::Slot> slot;
    std::shared_ptr<Bridge> bridge;
    std::unique_ptr<HealthyRetirement::CaptureController> controller;
    FrameKey target;
    int context = 0;
    std::uint64_t last = 0;
    Issuer() {
      slot = std::make_shared<HealthyRetirement::Slot>(17, 100, [] { return std::int64_t{10}; });
      CHECK(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context));
      HealthyRetirement::Operations ops;
      ops.currentIdentity = [] { return Identity{11, 12, 13}; };
      ops.unregisterCallback = [](void*) {};
      ops.invalidateQueued = [](std::uint64_t) { return true; };
      ops.freeContext = [](void*) {};
      ops.clearPointer = [](void*) { return true; };
      CHECK(slot->install({11, 12, 13}, &context, ops));
      controller = HealthyRetirement::CaptureController::create(slot);
      CHECK(controller); bridge = slot->captureBridge(); CHECK(bridge);
      CHECK(bridge->rendererCreated({11, 12, 13}));
      CHECK(bridge->framebuffer({11, 12, 13}, Fbo{7, 160, 90, 0, 0, 0, true, true}));
    }
  };
  static std::shared_ptr<Issuer> current;
  static std::vector<std::shared_ptr<Issuer>> retained;
public:
  static std::shared_ptr<const HealthyRetirement::RetirementClaim> claim;
  static void revoke(int mode) {
    if (mode == 0) CHECK(current->bridge->framebuffer({11, 12, 13}, Fbo{8, 160, 90, 0, 0, 0, true, true}));
    if (mode == 1) current->slot->invalidate(17);
    if (mode == 2) { current->controller.reset(); current->bridge.reset(); }
    if (mode == 3) current->controller->cancel();
    if (mode == 4) CHECK(!current->bridge->framebufferCurrent({11, 99, 13}, Fbo{7, 160, 90, 0, 0, 0, true, true}));
  }
  static void attach(const std::shared_ptr<FrameRecord>& record, bool baseline) {
    auto& r = *record;
    if (baseline) {
      current = std::make_shared<Issuer>(); retained.push_back(current);
      current->target = r.key;
      r.key.seek = 0; r.key.phase = 4;
      CHECK(current->controller->request(r.key, 9, Bridge::Phase::Baseline, claim));
    } else if (current->last == 1) {
      CHECK(current->controller->request(current->target, 9, Bridge::Phase::Target, claim));
    }
    const auto serial = std::max(r.serial, current->last + 1);
    CHECK(current->bridge->framebufferCurrent({11, 12, 13}, Fbo{7, 160, 90, 0, 0, 0, true, true}));
    auto ticket = current->bridge->issue({11, 12, 13}, serial);
    CHECK(current->bridge->consume(ticket, {11, 12, 13}));
    r.captureEvidence = current->bridge->evidence(ticket, {11, 12, 13}, r);
    CHECK(r.captureEvidence); current->last = serial;
  }
};
std::shared_ptr<TestCaptureCapability::Issuer> TestCaptureCapability::current;
std::vector<std::shared_ptr<TestCaptureCapability::Issuer>> TestCaptureCapability::retained;
std::shared_ptr<const HealthyRetirement::RetirementClaim> TestCaptureCapability::claim;

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
Capture frame(const FrameKey& key, int index, std::uint64_t serial, std::uint64_t time,
              const std::function<void(FrameRecord&)>& mutate = {})
{
  auto r = std::make_shared<FrameRecord>();
  r->key = key; r->serial = serial; r->width = 160; r->height = 90;
  r->thread = 11; r->actualContext = 12; r->capturedAt = time;
  r->rgba.assign(160 * 90 * 4, 0);
  for (std::size_t i = 3; i < r->rgba.size(); i += 4) r->rgba[i] = 255;
  for (int bit = 0; bit < 9; ++bit) paint(*r, 5 + bit * 16, 8, 12, 12, index & (1 << bit) ? 220 : 70);
  paint(*r, (index * 3) % 140, 35, 20, 20, 220);
  if (mutate) mutate(*r);
  TestCaptureCapability::attach(r, index == 0);
  return r;
}
PausedHoldBinding binding()
{
  PausedHoldBinding b;
  b.frame = {1, 2, 3, 4, 5, 6}; b.host = 7; b.revision = 8; b.attempt = 9; b.grant = 10;
  b.checkpointMs = 3000; b.deadlineNs = 20000000000LL;
  b.claim = std::make_shared<HealthyRetirement::RetirementClaim>(17);
  TestCaptureCapability::claim = b.claim;
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
    for (int scenario = 0; scenario != 4; ++scenario) {
      auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
      auto hold = TestPausedHold::make(b, proof);
      ResumeProgress progress(b, proof, 2, dispatch);
      auto key = b.frame; if (scenario == 2) ++key.entry;
      progress.capture(frame(key, 91, 3, scenario == 3 ? dispatch - 1 : dispatch + 1));
      progress.capture(frame(key, scenario == 0 ? 91 : 121, 4,
                             dispatch + (scenario == 1 ? 1000000000LL : 1000000001LL)));
      observations(progress); CHECK(!progress.finish(dispatch + 1000000001LL));
    }
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
    for (int scenario = 0; scenario < 5; ++scenario) {
      auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
      auto hold = TestPausedHold::make(b, proof);
      ResumeProgress progress(b, proof, 2, dispatch);
      auto record = frame(b.frame, 91, 3, dispatch + 1, [scenario](FrameRecord& value) {
        if (scenario == 0) ++value.thread;
        if (scenario == 1) ++value.actualContext;
        if (scenario == 2) value.serial = 2;
        if (scenario == 3) value.rgba.clear();
        if (scenario == 4) value.capturedAt = std::numeric_limits<std::uint64_t>::max();
      });
      progress.capture(record);
      progress.capture(frame(b.frame, 121, 4, dispatch + 1000000001LL));
      observations(progress); CHECK(!progress.finish(dispatch + 1000000001LL));
    }
    auto b = binding(); std::shared_ptr<const PausedHoldProof> proof;
    auto hold = TestPausedHold::make(b, proof);
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
  static void captureGuardRevocation()
  {
    for (int mode = 0; mode != 5; ++mode) {
      auto b = binding(); std::shared_ptr<const PausedHoldProof> holdProof;
      auto hold = TestPausedHold::make(b, holdProof);
      ResumeProgress progress(b, holdProof, 2, dispatch);
      frames(progress, b); observations(progress);
      const auto proof = progress.finish(dispatch + 1000000001LL);
      CHECK(proof && proof->matches(b, dispatch + 1000000001LL));
      TestCaptureCapability::revoke(mode);
      CHECK(!proof->matches(b, dispatch + 1000000001LL));
    }
  }
  static void rejectMissingEvidence()
  {
    for (int missing = 0; missing != 2; ++missing) {
      auto b = binding(); std::shared_ptr<const PausedHoldProof> holdProof;
      auto hold = TestPausedHold::make(b, holdProof);
      ResumeProgress progress(b, holdProof, 2, dispatch);
      auto first = std::make_shared<FrameRecord>(*frame(b.frame, 91, 3, dispatch + 1));
      auto second = std::make_shared<FrameRecord>(*frame(b.frame, 121, 4, dispatch + 1000000001LL));
      (missing == 0 ? first : second)->captureEvidence.reset();
      progress.capture(first); progress.capture(second); observations(progress);
      CHECK(!progress.finish(dispatch + 1000000001LL));
    }
  }
  static void rejectAlteredPixels()
  {
    for (int altered = 0; altered != 2; ++altered) {
      auto b = binding(); std::shared_ptr<const PausedHoldProof> holdProof;
      auto hold = TestPausedHold::make(b, holdProof);
      ResumeProgress progress(b, holdProof, 2, dispatch);
      auto first = std::make_shared<FrameRecord>(*frame(b.frame, 91, 3, dispatch + 1));
      auto second = std::make_shared<FrameRecord>(*frame(b.frame, 121, 4, dispatch + 1000000001LL));
      (altered == 0 ? first : second)->rgba[0] ^= 1;
      CHECK(decodeFrame(*first) == 91 && decodeFrame(*second) == 121);
      progress.capture(first); progress.capture(second); observations(progress);
      CHECK(!progress.finish(dispatch + 1000000001LL));
    }
  }
public:
  static void run() { goodAndLifetime(); imageFailures(); observationFailures(); revokedAndDestroyed(); rejectBadCaptures(); holdRevocation(); captureIsCopied(); captureGuardRevocation(); rejectMissingEvidence(); rejectAlteredPixels(); }
};
int main() { TestResumeProgress::run(); }
