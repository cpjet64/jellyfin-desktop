#pragma once
#include "frame_acceptance.h"
#include "lifetime.h"
#include <memory>
#include <mutex>

class MpvRenderer;
class TestCaptureCapability;
class QImage;
namespace HealthyRetirement {
class FixtureGraph;
class CaptureBridge;
class CaptureController;
struct FramebufferObservation {
  std::uint64_t attachment = 0;
  int width = 0, height = 0, level = -1, face = -1, layer = -1;
  bool bound = false, queried = false;
};
class CaptureEvidence final {
public:
  bool matches(const fixture_restart::FrameRecord& record, std::uint64_t attempt, bool target) const;
private:
  friend class CaptureBridge;
  CaptureEvidence(std::weak_ptr<CaptureBridge> owner, std::uint64_t renderer,
                  std::uint64_t framebuffer, std::uint64_t request, bool target,
                  fixture_restart::FrameKey key, std::uint64_t attempt, std::uint64_t serial,
                  std::shared_ptr<const fixture_restart::FrameRecord> record)
    : owner_(std::move(owner)), renderer_(renderer), framebuffer_(framebuffer),
      request_(request), target_(target), key_(key), attempt_(attempt), serial_(serial), record_(std::move(record)) {}
  std::weak_ptr<CaptureBridge> owner_;
  const std::uint64_t renderer_, framebuffer_, request_;
  const bool target_;
  const fixture_restart::FrameKey key_;
  const std::uint64_t attempt_, serial_;
  const std::shared_ptr<const fixture_restart::FrameRecord> record_;
};

// A graph owns this bridge; the renderer may only borrow it through the Slot.
// An acknowledgment remains useful only while ready() still succeeds.
class CaptureBridge final : public std::enable_shared_from_this<CaptureBridge> {
public:
  enum class Phase { Baseline, Target };
  enum class Status { Pending, Ready, Rejected };
  bool ready(std::uint64_t attempt, Phase phase) const;
  void cancel() noexcept;
private:
  friend class ::MpvRenderer;
  friend class CaptureController;
  friend class CaptureEvidence;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestCaptureCapability;
#endif
  explicit CaptureBridge(std::weak_ptr<Slot> slot);
  bool request(fixture_restart::FrameKey key, std::uint64_t attempt, Phase phase,
               std::shared_ptr<const RetirementClaim> claim);
  Status status(std::uint64_t attempt, Phase phase) const;
  bool admitTarget(const fixture_restart::FrameKey& key, std::uint64_t attempt,
                   const std::shared_ptr<const RetirementClaim>& claim,
                   RetirementClaim::ArmIdentity identity) const;
  struct Ticket {
    fixture_restart::FrameKey key;
    std::uint64_t request = 0, renderer = 0, framebuffer = 0, serial = 0;
    bool first = false;
  };
  bool rendererCreated(Identity identity);
  bool framebuffer(Identity identity, const FramebufferObservation& observation);
  bool framebufferCurrent(Identity identity, const FramebufferObservation& observation);
  Ticket issue(Identity identity, std::uint64_t serial);
  bool consume(const Ticket& ticket, Identity identity);
  std::shared_ptr<const CaptureEvidence> evidence(const Ticket& ticket, Identity identity,
                                                  const fixture_restart::FrameRecord& record);
  void publish(const Ticket& ticket, Identity identity, QImage image, std::uint64_t updateFlags,
               int infoResult, std::uint64_t nextFlags, int renderResult);
  bool evidenceValid(std::uint64_t renderer, std::uint64_t framebuffer,
                     std::uint64_t request, bool target) const;
  void rendererDestroyed() noexcept;
  bool live(Identity identity) const;
  std::weak_ptr<Slot> slot_;
  mutable std::mutex mutex_;
  fixture_restart::FrameKey key_;
  fixture_restart::FrameKey baseline_;
  std::shared_ptr<const RetirementClaim> claim_;
  Identity original_{};
  std::uint64_t generation_ = 0, renderer_ = 0, framebuffer_ = 0, attempt_ = 0;
  std::uint64_t lastIssued_ = 0, lastConsumed_ = 0, evidenceIssued_ = 0, baselineRequest_ = 0;
  std::uint64_t attachment_ = 0;
  int width_ = 0, height_ = 0;
  Phase phase_ = Phase::Baseline;
  bool requested_ = false, acknowledged_ = false, destroyed_ = false, baselineIssued_ = false;
  bool framebufferQualified_ = false;
};

// Graph-side facade cannot issue renderer tickets or construct evidence.
class CaptureController final {
public:
  static std::unique_ptr<CaptureController> create(const std::shared_ptr<Slot>& slot);
  bool request(fixture_restart::FrameKey key, std::uint64_t attempt, CaptureBridge::Phase phase,
               std::shared_ptr<const RetirementClaim> claim);
  bool ready(std::uint64_t attempt, CaptureBridge::Phase phase) const;
  CaptureBridge::Status status(std::uint64_t attempt, CaptureBridge::Phase phase) const;
  bool admitTarget(const fixture_restart::FrameKey& key, std::uint64_t attempt,
                   const std::shared_ptr<const RetirementClaim>& claim,
                   RetirementClaim::ArmIdentity identity) const;
  void cancel() noexcept;
private:
  explicit CaptureController(std::shared_ptr<CaptureBridge> bridge) : bridge_(std::move(bridge)) {}
  std::shared_ptr<CaptureBridge> bridge_;
};
}
