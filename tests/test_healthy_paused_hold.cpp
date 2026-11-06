#include "healthy_restart/paused_hold.h"
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
    if (!current->bridge->consume(ticket, {11, 12, 13})) {
      std::fprintf(stderr, "capture failed baseline=%d serial=%llu last=%llu claim=%d ticket=%llu fixture=%zu\n", baseline,
                   static_cast<unsigned long long>(serial), static_cast<unsigned long long>(current->last), claim->open(),
                   static_cast<unsigned long long>(ticket.request), retained.size());
      std::abort();
    }
    r.captureEvidence = current->bridge->evidence(ticket, {11, 12, 13}, r);
    CHECK(r.captureEvidence); current->last = serial;
  }
};
std::shared_ptr<TestCaptureCapability::Issuer> TestCaptureCapability::current;
std::vector<std::shared_ptr<TestCaptureCapability::Issuer>> TestCaptureCapability::retained;
std::shared_ptr<const HealthyRetirement::RetirementClaim> TestCaptureCapability::claim;

namespace {
constexpr std::int64_t start = 1000000000LL;
constexpr std::int64_t step = 200000000LL;

void paint(FrameRecord& r, int x, int y, int width, int height, int value)
{
  for (int row = y; row < y + height; ++row) for (int col = x; col < x + width; ++col) {
    const auto offset = std::size_t((row * r.width + col) * 4);
    r.rgba[offset] = r.rgba[offset + 1] = r.rgba[offset + 2] = std::uint8_t(value);
  }
}
Capture frame(const FrameKey& key, int index, std::uint64_t serial, std::uint64_t capturedAt,
              const std::function<void(FrameRecord&)>& mutate = {})
{
  auto r = std::make_shared<FrameRecord>();
  r->key = key; r->serial = serial; r->width = 160; r->height = 90;
  r->thread = 11; r->actualContext = 12; r->capturedAt = capturedAt;
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
  b.frame = {1, 2, 3, 4, 5, 6};
  b.host = 7; b.revision = 8; b.attempt = 9; b.grant = 10;
  b.checkpointMs = 3000; b.deadlineNs = 10000000000LL;
  b.claim = std::make_shared<HealthyRetirement::RetirementClaim>(17);
  TestCaptureCapability::claim = b.claim;
  return b;
}
}

class TestPausedHold {
  static void prepare(PausedHold& hold, const PausedHoldBinding& b)
  {
    CHECK(hold.baseline(frame(b.frame, 0, 1, start - 1)));
    CHECK(hold.arm(1, start));
  }
  static void sample(PausedHold& hold, int index, std::int64_t position = 3000)
  {
    const auto time = start + std::int64_t(index) * step;
    CHECK(hold.observe(std::uint64_t(index + 1), time, time, position));
  }
  static std::shared_ptr<const PausedHoldProof> complete(PausedHold& hold, const PausedHoldBinding& b)
  {
    prepare(hold, b);
    hold.capture(frame(b.frame, 90, 2, start + 1)); // target precedes queued observation acknowledgement
    for (int i = 0; i <= 15; ++i) sample(hold, i);
    return hold.finish(start + 15 * step);
  }
  static void goodAndBinding()
  {
    auto b = binding(); PausedHold hold(b);
    const auto proof = complete(hold, b); CHECK(proof && proof->matches(b, start + 15 * step));
    auto wrong = b; ++wrong.frame.entry; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.frame.seek; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.frame.process; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.frame.context; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.frame.phase; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.host; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.revision; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.attempt; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; ++wrong.grant; CHECK(!proof->matches(wrong, start + 15 * step));
    wrong = b; wrong.claim = std::make_shared<HealthyRetirement::RetirementClaim>(17);
    CHECK(!proof->matches(wrong, start + 15 * step));
    CHECK(!proof->matches(b, start + 15 * step - 1));
    CHECK(!proof->matches(b, start + 15 * step + 250000001LL));
    hold.invalidate(); CHECK(!proof->matches(b, start + 15 * step));
  }
  static void timeAndPosition()
  {
    auto b = binding(); PausedHold shortHold(b); prepare(shortHold, b);
    shortHold.capture(frame(b.frame, 90, 2, start + 1));
    for (int i = 0; i < 15; ++i) sample(shortHold, i);
    CHECK(!shortHold.finish(start + 14 * step));
    sample(shortHold, 15); CHECK(shortHold.finish(start + 15 * step));
    PausedHold gap(b); prepare(gap, b); sample(gap, 0);
    CHECK(!gap.observe(2, start + 250000001LL, start + 250000001LL, 3000));
    PausedHold backwards(b); prepare(backwards, b); sample(backwards, 0);
    CHECK(!backwards.observe(1, start + step, start + step, 3000));
    PausedHold wrongPosition(b); prepare(wrongPosition, b);
    CHECK(!wrongPosition.observe(1, start, start, 3101));
    PausedHold drift(b); prepare(drift, b); sample(drift, 0);
    CHECK(!drift.observe(2, start + step, start + step, 3251));
    PausedHold extreme(b); prepare(extreme, b);
    CHECK(!extreme.observe(1, start, std::numeric_limits<std::int64_t>::max(), 3000));
  }
  static void captureAndLifetime()
  {
    auto b = binding(); PausedHold missing(b); prepare(missing, b);
    for (int i = 0; i <= 15; ++i) sample(missing, i);
    CHECK(!missing.finish(start + 15 * step));
    PausedHold wrong(b); prepare(wrong, b);
    auto key = b.frame; ++key.core; wrong.capture(frame(key, 90, 2, start + 1));
    for (int i = 0; i <= 15; ++i) sample(wrong, i);
    CHECK(!wrong.finish(start + 15 * step));
    std::shared_ptr<const PausedHoldProof> proof;
    { PausedHold temporary(b); proof = complete(temporary, b); CHECK(proof); }
    CHECK(!proof->matches(b, start + 15 * step));
  }
  static void revokedAndUnqualifiedImages()
  {
    auto b = binding(); PausedHold revoked(b); prepare(revoked, b);
    std::const_pointer_cast<HealthyRetirement::RetirementClaim>(b.claim)->revoke();
    CHECK(!revoked.observe(1, start, start, 3000));
    b = binding(); PausedHold invalidImage(b); prepare(invalidImage, b);
    invalidImage.capture(frame(b.frame, 30, 2, start + 1));
    for (int i = 0; i <= 15; ++i) sample(invalidImage, i);
    CHECK(!invalidImage.finish(start + 15 * step));
    PausedHold oldCapture(b); prepare(oldCapture, b);
    oldCapture.capture(frame(b.frame, 90, 2, start - 1));
    for (int i = 0; i <= 15; ++i) sample(oldCapture, i);
    CHECK(!oldCapture.finish(start + 15 * step));
    PausedHold wrongSerial(b); prepare(wrongSerial, b);
    wrongSerial.capture(frame(b.frame, 90, 1, start + 1));
    for (int i = 0; i <= 15; ++i) sample(wrongSerial, i);
    CHECK(!wrongSerial.finish(start + 15 * step));
    PausedHold wrongContext(b); prepare(wrongContext, b);
    auto image = frame(b.frame, 90, 2, start + 1, [](FrameRecord& value) { ++value.actualContext; });
    wrongContext.capture(image);
    for (int i = 0; i <= 15; ++i) sample(wrongContext, i);
    CHECK(!wrongContext.finish(start + 15 * step));
  }
  static void timingBoundaries()
  {
    auto b = binding(); PausedHold exactGap(b); prepare(exactGap, b);
    CHECK(exactGap.observe(1, start, start, 3000));
    CHECK(exactGap.observe(2, start + 250000000LL, start + 250000000LL, 3000));
    PausedHold reordered(b); prepare(reordered, b);
    CHECK(reordered.observe(1, start, start + 10, 3000));
    CHECK(!reordered.observe(2, start + 9, start + 20, 3000));
    PausedHold negative(b); prepare(negative, b);
    CHECK(!negative.observe(1, -1, start, 3000));
    auto expired = binding(); expired.deadlineNs = start;
    PausedHold expiredHold(expired);
    CHECK(expiredHold.baseline(frame(expired.frame, 0, 1, start - 1)));
    CHECK(!expiredHold.arm(1, start));
  }
  static void targetTolerance()
  {
    auto b = binding(); PausedHold edge(b); prepare(edge, b);
    edge.capture(frame(b.frame, 93, 2, start + 1));
    for (int i = 0; i <= 15; ++i) sample(edge, i);
    CHECK(edge.finish(start + 15 * step));
    PausedHold beyond(b); prepare(beyond, b);
    beyond.capture(frame(b.frame, 94, 2, start + 1));
    for (int i = 0; i <= 15; ++i) sample(beyond, i);
    CHECK(!beyond.finish(start + 15 * step));
  }
  static void captureGuardRevocation()
  {
    for (int mode = 0; mode != 5; ++mode) {
      auto b = binding(); PausedHold hold(b);
      const auto proof = complete(hold, b);
      CHECK(proof && proof->matches(b, start + 15 * step));
      TestCaptureCapability::revoke(mode);
      CHECK(!proof->matches(b, start + 15 * step));
    }
  }
  static void rejectMissingEvidence()
  {
    auto b = binding(); PausedHold missingBaseline(b);
    auto baseline = std::make_shared<FrameRecord>(*frame(b.frame, 0, 1, start - 1));
    baseline->captureEvidence.reset(); CHECK(!missingBaseline.baseline(baseline));
    PausedHold missingTarget(b); prepare(missingTarget, b);
    auto target = std::make_shared<FrameRecord>(*frame(b.frame, 90, 2, start + 1));
    target->captureEvidence.reset(); missingTarget.capture(target);
    for (int i = 0; i <= 15; ++i) sample(missingTarget, i);
    CHECK(!missingTarget.finish(start + 15 * step));
  }
  static void invertedRowsAreRejected()
  {
    auto b = binding();
    auto baseline = frame(b.frame, 0, 1, start - 1);
    auto target = std::make_shared<FrameRecord>(*frame(b.frame, 90, 2, start + 1));
    CHECK(decodeFrame(*target) == 90);
    constexpr std::size_t rowBytes = 160 * 4;
    for (std::size_t y = 0; y < 45; ++y)
      std::swap_ranges(target->rgba.begin() + y * rowBytes,
                       target->rgba.begin() + (y + 1) * rowBytes,
                       target->rgba.begin() + (89 - y) * rowBytes);
    CHECK(!decodeFrame(*target));
    FrameAcceptance acceptance;
    CHECK(acceptance.baseline(baseline));
    CHECK(acceptance.arm(b.frame, 1, 3000));
    acceptance.capture(target); acceptance.acknowledge(b.frame, true, true, true);
    CHECK(!acceptance.accepted() && !acceptance.target());
  }
  static void rejectAlteredPixels()
  {
    auto b = binding(); PausedHold hold(b); prepare(hold, b);
    auto target = std::make_shared<FrameRecord>(*frame(b.frame, 90, 2, start + 1));
    const auto other = frame(b.frame, 91, 3, start + 2);
    target->rgba = other->rgba;
    CHECK(decodeFrame(*target) == 91);
    hold.capture(target);
    for (int i = 0; i <= 15; ++i) sample(hold, i);
    CHECK(!hold.finish(start + 15 * step));
  }
public:
  static void run() { goodAndBinding(); timeAndPosition(); captureAndLifetime(); revokedAndUnqualifiedImages(); timingBoundaries(); targetTolerance(); captureGuardRevocation(); rejectMissingEvidence(); invertedRowsAreRejected(); rejectAlteredPixels(); }
};
int main() { TestPausedHold::run(); }
