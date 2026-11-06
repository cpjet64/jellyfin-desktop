#include "capture_capability.h"
#include <atomic>
#include <limits>

namespace HealthyRetirement {
namespace {
std::atomic<std::uint64_t> nextRenderer{1};
bool same(Identity a, Identity b)
{
  return a.thread && a.context && a.epoch && a.thread == b.thread &&
         a.context == b.context && a.epoch == b.epoch;
}
bool qualified(const FramebufferObservation& value)
{
  return value.queried && value.bound && value.attachment && value.width == 160 &&
         value.height == 90 && value.level == 0 && value.face == 0 && value.layer == 0;
}
bool sameRecord(const fixture_restart::FrameRecord& a, const fixture_restart::FrameRecord& b)
{
  return a.key == b.key && a.serial == b.serial && a.width == b.width && a.height == b.height &&
         a.rgba == b.rgba && a.updateResult == b.updateResult && a.infoResult == b.infoResult &&
         a.renderResult == b.renderResult && a.updateFlags == b.updateFlags && a.nextFlags == b.nextFlags &&
         a.thread == b.thread && a.actualContext == b.actualContext && a.capturedAt == b.capturedAt;
}
}

CaptureBridge::CaptureBridge(std::weak_ptr<Slot> slot) : slot_(std::move(slot)) {}

bool CaptureBridge::live(Identity identity) const
{
  const auto slot = slot_.lock();
  const auto lifetime = slot ? slot->current() : nullptr;
  if (!slot || !lifetime || !claim_ || !claim_->open() ||
      claim_->generation() != slot->generation() || !same(identity, original_) ||
      !same(lifetime->originalIdentity(), original_)) return false;
  return slot->captureAdmission(lifetime);
}

bool CaptureBridge::request(fixture_restart::FrameKey key, std::uint64_t attempt,
                            Phase phase, std::shared_ptr<const RetirementClaim> claim)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = slot_.lock();
  const bool keyValid = key.process && key.core && key.context && key.entry && key.phase;
  const bool target = phase == Phase::Target;
  const bool phaseValid = phase == Phase::Baseline || target;
  const bool sameOwner = key.process == baseline_.process && key.core == baseline_.core &&
      key.context == baseline_.context && key.entry == baseline_.entry;
  if (!slot || !attempt || !keyValid || !phaseValid || !claim || !claim->open() ||
      (target && (!key.seek || !baselineIssued_ || !sameOwner || key.phase == baseline_.phase || attempt != attempt_ ||
                  claim != claim_ || phase_ == Phase::Target)) ||
      (!target && (key.seek || baselineIssued_ || !framebufferQualified_)) ||
      claim->generation() != slot->generation() || destroyed_ ||
      generation_ == std::numeric_limits<std::uint64_t>::max()) return false;
  ++generation_;
  key_ = key;
  attempt_ = attempt;
  phase_ = phase;
  claim_ = std::move(claim);
  requested_ = true;
  acknowledged_ = false;
  return true;
}

bool CaptureBridge::ready(std::uint64_t attempt, Phase phase) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return requested_ && acknowledged_ && attempt == attempt_ && phase == phase_ &&
         framebufferQualified_ && attachment_ && width_ == 160 && height_ == 90 && live(original_);
}

CaptureBridge::Status CaptureBridge::status(std::uint64_t attempt, Phase phase) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = slot_.lock();
  const auto lifetime = slot ? slot->current() : nullptr;
  if (!requested_ || destroyed_ || !attempt || attempt != attempt_ || phase != phase_ ||
      !claim_ || !claim_->open() || !slot || !lifetime ||
      !same(original_, lifetime->originalIdentity()) || !slot->captureSnapshot(lifetime) ||
      !renderer_ || !framebuffer_ || !framebufferQualified_ || !attachment_ || width_ != 160 || height_ != 90)
    return Status::Rejected;
  return acknowledged_ ? Status::Ready : Status::Pending;
}

bool CaptureBridge::admitTarget(const fixture_restart::FrameKey& key, std::uint64_t attempt,
                                const std::shared_ptr<const RetirementClaim>& claim,
                                RetirementClaim::ArmIdentity identity) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = slot_.lock();
  const auto lifetime = slot ? slot->current() : nullptr;
  if (!requested_ || !acknowledged_ || destroyed_ || phase_ != Phase::Target ||
      !attempt || attempt != attempt_ || !(key == key_) || !claim || claim != claim_ ||
      identity.attempt != attempt || identity.core != key.core || !slot || !lifetime ||
      !same(original_, lifetime->originalIdentity()) ||
      !renderer_ || !framebuffer_ || !framebufferQualified_ || !attachment_ || width_ != 160 || height_ != 90)
    return false;
  return slot->admitCaptureDispatch(lifetime, claim, identity);
}

void CaptureBridge::cancel() noexcept
{
  std::lock_guard<std::mutex> lock(mutex_);
  requested_ = acknowledged_ = false;
  baselineIssued_ = false;
  claim_.reset();
}

bool CaptureBridge::rendererCreated(Identity identity)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const auto slot = slot_.lock();
  const auto lifetime = slot ? slot->current() : nullptr;
  if (destroyed_ || renderer_ || !lifetime || !same(identity, lifetime->originalIdentity())) return false;
  auto current = nextRenderer.load();
  while (current && current != std::numeric_limits<std::uint64_t>::max()) {
    if (nextRenderer.compare_exchange_weak(current, current + 1)) {
      renderer_ = current;
      original_ = identity;
      return true;
    }
  }
  return false;
}

bool CaptureBridge::framebuffer(Identity identity, const FramebufferObservation& observation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!renderer_ || destroyed_ || !same(identity, original_) ||
      framebuffer_ == std::numeric_limits<std::uint64_t>::max()) {
    requested_ = acknowledged_ = baselineIssued_ = framebufferQualified_ = false;
    return false;
  }
  ++framebuffer_;
  attachment_ = observation.attachment;
  width_ = observation.width;
  height_ = observation.height;
  framebufferQualified_ = qualified(observation);
  requested_ = acknowledged_ = false;
  baselineIssued_ = false;
  return framebufferQualified_;
}

bool CaptureBridge::framebufferCurrent(Identity identity, const FramebufferObservation& observation)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool matches = framebufferQualified_ && same(identity, original_) && qualified(observation) &&
                       observation.attachment == attachment_ && observation.width == width_ &&
                       observation.height == height_;
  if (!matches) { requested_ = acknowledged_ = baselineIssued_ = framebufferQualified_ = false; }
  return matches;
}

CaptureBridge::Ticket CaptureBridge::issue(Identity identity, std::uint64_t serial)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!requested_ || !renderer_ || !framebuffer_ || !framebufferQualified_ || !attachment_ || serial <= lastIssued_ ||
      width_ != 160 || height_ != 90 || !live(identity)) return {};
  if (phase_ == Phase::Baseline) { baseline_ = key_; baselineIssued_ = true; baselineRequest_ = generation_; }
  const bool first = !acknowledged_;
  acknowledged_ = true;
  lastIssued_ = serial;
  return {key_, generation_, renderer_, framebuffer_, serial, first};
}

bool CaptureBridge::consume(const Ticket& ticket, Identity identity)
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool valid = requested_ && acknowledged_ && ticket.request && ticket.request == generation_ &&
         ticket.renderer == renderer_ && ticket.framebuffer == framebuffer_ &&
         ticket.key == key_ && ticket.serial > lastConsumed_ && ticket.serial == lastIssued_ && live(identity);
  if (valid) lastConsumed_ = ticket.serial;
  return valid;
}

std::shared_ptr<const CaptureEvidence> CaptureBridge::evidence(
    const Ticket& ticket, Identity identity, const fixture_restart::FrameRecord& record)
{
  std::lock_guard<std::mutex> lock(mutex_);
  if (!requested_ || !acknowledged_ || ticket.request != generation_ ||
      ticket.renderer != renderer_ || ticket.framebuffer != framebuffer_ ||
      !(ticket.key == key_) || !ticket.serial || ticket.serial != lastConsumed_ ||
      ticket.serial <= evidenceIssued_ || record.captureEvidence || !live(identity)) return {};
  evidenceIssued_ = ticket.serial;
  return std::shared_ptr<const CaptureEvidence>(new CaptureEvidence(
      weak_from_this(), renderer_, framebuffer_, generation_, phase_ == Phase::Target, key_, attempt_,
      ticket.serial, std::make_shared<const fixture_restart::FrameRecord>(record)));
}

bool CaptureBridge::evidenceValid(std::uint64_t renderer, std::uint64_t framebuffer,
                                  std::uint64_t request, bool target) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  const bool phaseValid = target ? phase_ == Phase::Target && request == generation_
                                 : baselineIssued_ && request == baselineRequest_;
  return !destroyed_ && requested_ && framebufferQualified_ && phaseValid && renderer && renderer == renderer_ &&
         framebuffer && framebuffer == framebuffer_ && live(original_);
}

bool CaptureEvidence::matches(const fixture_restart::FrameRecord& record, std::uint64_t attempt, bool target) const
{
  const auto owner = owner_.lock();
  return owner && record_ && sameRecord(*record_, record) && record.key == key_ &&
         attempt == attempt_ && target == target_ && record.serial == serial_ &&
         owner->evidenceValid(renderer_, framebuffer_, request_, target_);
}

void CaptureBridge::rendererDestroyed() noexcept
{
  std::lock_guard<std::mutex> lock(mutex_);
  destroyed_ = true;
  requested_ = acknowledged_ = false;
}

std::unique_ptr<CaptureController> CaptureController::create(const std::shared_ptr<Slot>& slot)
{
  if (!slot) return {};
  auto bridge = std::shared_ptr<CaptureBridge>(new CaptureBridge(slot));
  if (!slot->bindCaptureBridge(bridge)) return {};
  return std::unique_ptr<CaptureController>(new CaptureController(std::move(bridge)));
}

bool CaptureController::request(fixture_restart::FrameKey key, std::uint64_t attempt,
                                CaptureBridge::Phase phase, std::shared_ptr<const RetirementClaim> claim)
{
  return bridge_->request(key, attempt, phase, std::move(claim));
}

bool CaptureController::ready(std::uint64_t attempt, CaptureBridge::Phase phase) const
{
  return bridge_->ready(attempt, phase);
}

CaptureBridge::Status CaptureController::status(std::uint64_t attempt, CaptureBridge::Phase phase) const
{
  return bridge_->status(attempt, phase);
}

bool CaptureController::admitTarget(const fixture_restart::FrameKey& key, std::uint64_t attempt,
                                    const std::shared_ptr<const RetirementClaim>& claim,
                                    RetirementClaim::ArmIdentity identity) const
{
  return bridge_->admitTarget(key, attempt, claim, identity);
}

void CaptureController::cancel() noexcept { bridge_->cancel(); }
}
