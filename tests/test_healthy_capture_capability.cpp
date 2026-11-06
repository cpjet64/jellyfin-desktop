#include "capture_capability.h"
#include "frame_capture.h"
#include <QImage>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>

using namespace HealthyRetirement;
using fixture_restart::FrameKey;
#define check(value) do { if (!(value)) { std::fprintf(stderr, "check failed at line %d\n", __LINE__); std::abort(); } } while (false)

class TestCaptureCapability {
  static constexpr Identity identity{11, 12, 13};
  static FramebufferObservation fbo(std::uint64_t attachment = 7, int w = 160, int h = 90) {
    return {attachment, w, h, 0, 0, 0, true, true};
  }
  struct Fixture {
    std::atomic<std::int64_t> now{10};
    std::shared_ptr<Slot> slot = std::make_shared<Slot>(9, 100, [this] { return now.load(); });
    std::shared_ptr<RetirementClaim> claim = std::make_shared<RetirementClaim>(9);
    std::shared_ptr<Lifetime> lifetime;
    FrameKey key{1, 2, 3, 4, 5, 0};
    int context = 0;
    std::shared_ptr<CaptureBridge> bridge{new CaptureBridge(slot)};
    explicit Fixture(bool initialize = true) {
      Operations ops;
      ops.currentIdentity = [] { return identity; };
      ops.unregisterCallback = [](void*) {};
      ops.invalidateQueued = [](std::uint64_t) { return true; };
      ops.freeContext = [](void*) {};
      ops.clearPointer = [](void*) { return true; };
      check(slot->beginCreation() && slot->beforeCreate() && slot->recordCreated(&context));
      lifetime = slot->install(identity, &context, ops);
      check(static_cast<bool>(lifetime));
      if (initialize) {
        check(bridge->rendererCreated(identity));
        check(bridge->framebuffer(identity, fbo()));
      }
      fixture_restart::disarmCapture();
      (void)fixture_restart::takeCaptures();
    }
    fixture_restart::Capture publish(std::uint64_t serial) {
      const auto ticket = bridge->issue(identity, serial);
      QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
      bridge->publish(ticket, identity, image, 0, 0, 0, 0);
      auto records = fixture_restart::takeCaptures();
      return records.size() == 1 ? records.front() : fixture_restart::Capture{};
    }
  };
  static void issue_and_revoke() {
    Fixture f;
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
    auto record = f.publish(1);
    check(record && record->captureEvidence && f.bridge->ready(20, CaptureBridge::Phase::Baseline));
    check(record->captureEvidence->matches(*record, 20, false));
    check(!record->captureEvidence->matches(*record, 21, false));
    check(!record->captureEvidence->matches(*record, 20, true));
    auto copy = *record;
    check(record->captureEvidence->matches(copy, 20, false));
    copy.rgba[0] ^= 1;
    check(!record->captureEvidence->matches(copy, 20, false));
    check(!f.bridge->ready(21, CaptureBridge::Phase::Baseline));
    f.bridge->cancel();
    check(!record->captureEvidence->matches(*record, 20, false));
  }
  static void framebuffer_and_phase() {
    Fixture f;
    auto targetKey = f.key; targetKey.seek = 6; ++targetKey.phase;
    check(!f.bridge->request(targetKey, 20, CaptureBridge::Phase::Target, f.claim));
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
    auto baseline = f.publish(1); check(static_cast<bool>(baseline));
    check(!f.bridge->request(targetKey, 21, CaptureBridge::Phase::Target, f.claim));
    auto wrong = targetKey; ++wrong.entry;
    check(!f.bridge->request(wrong, 20, CaptureBridge::Phase::Target, f.claim));
    check(!f.bridge->request(targetKey, 20, CaptureBridge::Phase::Target, std::make_shared<RetirementClaim>(9)));
    check(f.bridge->request(targetKey, 20, CaptureBridge::Phase::Target, f.claim));
    auto target = f.publish(2);
    check(target && target->captureEvidence->matches(*target, 20, true));
    check(baseline->captureEvidence->matches(*baseline, 20, false));
    check(!baseline->captureEvidence->matches(*baseline, 20, true));
    check(f.bridge->framebuffer(identity, fbo()));
    check(!target->captureEvidence->matches(*target, 20, true));
    check(!f.bridge->request(targetKey, 20, CaptureBridge::Phase::Target, f.claim));
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
  }
  static void forged_and_replayed() {
    Fixture f;
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
    auto ticket = f.bridge->issue(identity, 1);
    auto forged = ticket; ++forged.renderer;
    QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
    f.bridge->publish(forged, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
    forged = ticket; ++forged.serial;
    f.bridge->publish(forged, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
    f.bridge->publish(ticket, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().size() == 1);
    f.bridge->publish(ticket, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
  }
  static void incomplete_or_late() {
    Fixture f;
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
    auto first = f.bridge->issue(identity, 1);
    QImage wrong(160, 89, QImage::Format_RGBA8888); wrong.fill(Qt::black);
    f.bridge->publish(first, identity, wrong, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
    auto late = f.bridge->issue(identity, 2);
    f.bridge->cancel();
    QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
    f.bridge->publish(late, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
  }
  static void original_deadline_cannot_revive() {
    Fixture f;
    check(f.bridge->request(f.key, 20, CaptureBridge::Phase::Baseline, f.claim));
    auto record = f.publish(1);
    check(record && record->captureEvidence->matches(*record, 20, false));
    f.now = 100;
    check(!record->captureEvidence->matches(*record, 20, false));
    f.now = 99;
    check(!record->captureEvidence->matches(*record, 20, false));
    Fixture backward;
    check(backward.bridge->request(backward.key, 20, CaptureBridge::Phase::Baseline, backward.claim));
    auto other = backward.publish(1);
    check(other && other->captureEvidence->matches(*other, 20, false));
    backward.now = 9;
    check(!other->captureEvidence->matches(*other, 20, false));
    backward.now = 11;
    check(!other->captureEvidence->matches(*other, 20, false));
  }
  static void physical_and_identity() {
    Fixture f(false);
    check(!f.bridge->rendererCreated(Identity{99, 12, 13}));
    check(!f.bridge->rendererCreated(Identity{11, 12, 99}));
    check(f.bridge->rendererCreated(identity));
    check(!f.bridge->framebuffer(Identity{11, 99, 13}, fbo()));
    auto bad = fbo(); bad.level = 1; check(!f.bridge->framebuffer(identity, bad));
    bad = fbo(); bad.face = 1; check(!f.bridge->framebuffer(identity, bad));
    bad = fbo(); bad.layer = 1; check(!f.bridge->framebuffer(identity, bad));
    bad = fbo(); bad.bound = false; check(!f.bridge->framebuffer(identity, bad));
    bad = fbo(); bad.queried = false; check(!f.bridge->framebuffer(identity, bad));
    check(!f.bridge->framebuffer(identity, fbo(7, 320, 180)));
    check(f.bridge->framebuffer(identity, fbo()));
    check(!f.bridge->framebufferCurrent(identity, fbo(8)));
  }
  static void lifetime_replacement_and_late() {
    Fixture first, replacement;
    check(first.bridge->request(first.key, 20, CaptureBridge::Phase::Baseline, first.claim));
    auto oldTicket = first.bridge->issue(identity, 1);
    first.bridge->rendererDestroyed();
    check(replacement.bridge->request(replacement.key, 20, CaptureBridge::Phase::Baseline, replacement.claim));
    QImage image(160, 90, QImage::Format_RGBA8888); image.fill(Qt::black);
    replacement.bridge->publish(oldTicket, identity, image, 0, 0, 0, 0);
    check(fixture_restart::takeCaptures().empty());
    auto record = replacement.publish(1); check(static_cast<bool>(record));
    check(replacement.slot->request(9) == Result::Pending);
    check(!record->captureEvidence->matches(*record, 20, false));
    replacement.bridge.reset(); replacement.slot.reset();
    check(!record->captureEvidence->matches(*record, 20, false));
  }
public:
  static void run() {
    issue_and_revoke(); framebuffer_and_phase(); forged_and_replayed(); incomplete_or_late();
    physical_and_identity(); lifetime_replacement_and_late(); original_deadline_cannot_revive();
  }
};
constexpr Identity TestCaptureCapability::identity;
int main() { TestCaptureCapability::run(); }
