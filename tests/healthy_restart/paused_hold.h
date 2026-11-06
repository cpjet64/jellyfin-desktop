#pragma once

#include "frame_acceptance.h"
#include "retirement_claim.h"
#include <atomic>
#include <cstdint>
#include <memory>

class PlayerComponent;
class TestPausedHold;

namespace HealthyRestart {
class ResumeProgress;
class ResumeProgressProof;
struct PausedHoldBinding {
  fixture_restart::FrameKey frame;
  std::uint64_t host = 0, revision = 0, attempt = 0, grant = 0;
  std::int64_t checkpointMs = -1, deadlineNs = 0;
  std::shared_ptr<const HealthyRetirement::RetirementClaim> claim;
};

// An immutable result; only the validator can construct one.
class PausedHoldProof final {
  friend class PausedHold;
  friend class ResumeProgress;
  friend class ResumeProgressProof;
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestPausedHold;
#endif
  PausedHoldProof(PausedHoldBinding binding, fixture_restart::Capture baseline, fixture_restart::Capture target,
                  std::shared_ptr<const std::atomic_bool> validity, std::int64_t lastRequestNs,
                  std::int64_t completedNs);
  bool matches(const PausedHoldBinding& binding, std::int64_t nowNs) const;
  bool retained() const;
  const PausedHoldBinding binding_;
  const fixture_restart::Capture baseline_;
  const fixture_restart::Capture target_;
  const std::shared_ptr<const std::atomic_bool> validity_;
  const std::int64_t lastRequestNs_, completedNs_;
};

// Only the copied Player can submit ledger observations and mailbox captures.
class PausedHold final {
public:
  ~PausedHold();
private:
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestPausedHold;
#endif
  explicit PausedHold(PausedHoldBinding binding);
  bool baseline(fixture_restart::Capture record);
  bool arm(std::uint64_t afterSerial, std::int64_t armedNs);
  void capture(fixture_restart::Capture record);
  bool observe(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs,
               std::int64_t positionMs);
  std::shared_ptr<const PausedHoldProof> finish(std::int64_t nowNs);
  void invalidate();
  bool live(std::int64_t nowNs) const;
  PausedHoldBinding binding_;
  std::shared_ptr<std::atomic_bool> validity_ = std::make_shared<std::atomic_bool>(true);
  fixture_restart::FrameAcceptance frames_;
  fixture_restart::Capture baseline_;
  std::uint64_t lastId_ = 0;
  std::int64_t armedNs_ = 0, firstReceivedNs_ = 0, lastRequestNs_ = 0, lastReceivedNs_ = 0;
  std::int64_t firstPositionMs_ = -1;
  bool armed_ = false, invalid_ = false;
};
}
