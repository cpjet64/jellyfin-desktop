#pragma once
#include "paused_hold.h"
#include <atomic>
#include <memory>

class PlayerComponent;
class TestResumeProgress;
namespace HealthyRestart {
class ResumeProgress;
class ResumeProgressProof final {
  friend class ResumeProgress;
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestResumeProgress;
#endif
  ResumeProgressProof(PausedHoldBinding binding, std::shared_ptr<const PausedHoldProof> hold,
                      fixture_restart::Capture first, fixture_restart::Capture second,
                      std::shared_ptr<const std::atomic_bool> validity, std::int64_t completedNs);
  bool matches(const PausedHoldBinding& binding, std::int64_t nowNs) const;
  const PausedHoldBinding binding_;
  const std::shared_ptr<const PausedHoldProof> hold_;
  const fixture_restart::Capture first_, second_;
  const std::shared_ptr<const std::atomic_bool> validity_;
  const std::int64_t completedNs_;
};

// Only the copied Player can feed actual mailbox captures and ledger records.
class ResumeProgress final {
public:
  ~ResumeProgress();
private:
  friend class ::PlayerComponent;
#ifdef HEALTHY_RETIREMENT_TESTING
  friend class ::TestResumeProgress;
#endif
  ResumeProgress(PausedHoldBinding binding, std::shared_ptr<const PausedHoldProof> hold,
                 std::uint64_t afterSerial, std::int64_t dispatchNs);
  void capture(fixture_restart::Capture record);
  bool initial(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs, std::int64_t positionMs);
  bool advance(std::uint64_t id, std::int64_t requestedNs, std::int64_t receivedNs, std::int64_t positionMs);
  std::shared_ptr<const ResumeProgressProof> finish(std::int64_t nowNs);
  void invalidate();
  bool live(std::int64_t nowNs) const;
  PausedHoldBinding binding_;
  std::shared_ptr<const PausedHoldProof> hold_;
  std::shared_ptr<std::atomic_bool> validity_ = std::make_shared<std::atomic_bool>(true);
  fixture_restart::Capture first_, second_;
  std::uint64_t boundary_ = 0, lastObservationId_ = 0;
  std::int64_t dispatchNs_ = 0, lastReceivedNs_ = 0, initialPositionMs_ = -1;
  bool advanced_ = false, invalid_ = false;
};
}
