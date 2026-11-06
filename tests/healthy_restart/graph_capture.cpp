#include "graph.h"
#include "graph_session.h"
#include "capture_capability.h"
#include "paused_hold.h"
#include "module_channel.h"
#include "player/PlayerComponent.h"
#include <QThread>

namespace HealthyRetirement {
bool FixtureGraph::captureSessionReady() const
{
  const auto containment = containment_.lock();
  if (!dispatcher_ || dispatcher_->thread() != QThread::currentThread() || !access_ ||
      access_->phase.load() != AccessPhase::Normal || access_->revoked.load() ||
      !containment || !containment->intact() || !slot_ || retirementRequested_) return false;
#ifdef HEALTHY_RETIREMENT_TESTING
  if (captureSessionReadyForTest_) {
    try {
      const auto now = clock_();
      if (now < lastSessionNow_ || now >= normalDeadline_) return false;
      lastSessionNow_ = now;
      return captureSessionReadyForTest_() && access_->phase.load() == AccessPhase::Normal &&
          !access_->revoked.load() && containment->intact();
    } catch (...) { return false; }
  }
#endif
  return sessionReady(false);
}

bool FixtureGraph::capturePlayerBound() const
{
  if (!session_ || !player_ || !slot_) return false;
  HealthyModuleIdentity::LaunchBinding binding;
  std::uint64_t launch = 0;
#ifdef HEALTHY_RETIREMENT_TESTING
  if (captureSessionReadyForTest_) {
    if (!captureBindingForTest_) return false;
    binding = *captureBindingForTest_;
    launch = captureLaunchForTest_;
  } else
#endif
  if (!module_ || !module_->stillValid(slot_->generation()) ||
      !module_->launchBinding(slot_->generation(), binding) ||
      !module_->launchId(slot_->generation(), launch)) return false;
  const QByteArray digest(reinterpret_cast<const char*>(binding.expectedGrantDigest.data()),
                          int(binding.expectedGrantDigest.size()));
  return binding.role == HealthyModuleIdentity::LaunchRole::Restore && binding.run == session_->run &&
      launch == session_->launch && digest == session_->digest && session_->gate &&
      player_->m_restoreGate == session_->gate &&
      session_->run && session_->launch && session_->binding.grant && session_->binding.revision &&
      player_->m_restoreRun == session_->run && player_->m_restoreLaunch == session_->launch &&
      player_->m_restoreGrant == session_->binding.grant && player_->m_restoreRevision == session_->binding.revision &&
      player_->m_restoreDigest == session_->digest;
}

bool FixtureGraph::captureBaseline()
{
  if (!capture_ || captureBaselineRequested_ || !captureSessionReady() || !capturePlayerBound() ||
      !player_->restoreOwnsCurrent() || !player_->healthyUnpauseAuthorized(false) ||
      !player_->nativeObservationCurrent() || !player_->m_nativeObservation.paused ||
      player_->m_nativeObservation.positionMs < 0 || player_->m_nativeObservation.positionMs > 100 ||
      player_->m_nativeRestore.seekAttempted) return false;
  const auto key = player_->m_nativeRestore.key;
  const auto receipt = player_->m_nativeReceipts.snapshot();
  const auto claim = std::atomic_load(&access_->intentClaim);
  const auto lifetime = slot_->current();
  if (!lifetime || !session_->launch || !claim || claim != player_->m_healthyStopLease || !claim->open() ||
      key.host != session_->host || key.core != session_->core || key.revision != session_->intentRevision ||
      key.attempt != session_->attempt || receipt.key.host != key.host || receipt.key.core != key.core ||
      receipt.key.revision != key.revision || receipt.key.attempt != key.attempt || receipt.entry <= 0 ||
      !receipt.replyOk || !receipt.started || receipt.invalid || receipt.ended) return false;
  const auto epoch = lifetime->originalIdentity().epoch;
  if (!epoch) return false;
  const bool requested = capture_->request({session_->launch, key.core, epoch, static_cast<std::uint64_t>(receipt.entry), 1, 0},
                                           key.attempt, CaptureBridge::Phase::Baseline, claim);
  if (!requested) return false;
  captureBaselineRequested_ = true;
  if (requested && captureSessionReady() && claim->open()) return true;
  cancelCapture();
  return false;
}

bool FixtureGraph::captureTarget()
{
  if (!capture_ || !captureBaselineRequested_ || captureTargetRequested_ || !captureSessionReady() ||
      !capturePlayerBound() || !player_->restoreOwnsCurrent() || !player_->healthyUnpauseAuthorized(false) ||
      player_->m_nativeRestore.seekAttempted) return false;
  const auto key = player_->m_nativeRestore.key;
  const auto receipt = player_->m_nativeReceipts.snapshot();
  const auto claim = std::atomic_load(&access_->intentClaim);
  const auto lifetime = slot_->current();
  if (!lifetime || !session_->launch || !claim || claim != player_->m_healthyStopLease || !claim->open() ||
      key.host != session_->host || key.core != session_->core || key.revision != session_->intentRevision ||
      key.attempt != session_->attempt || receipt.key.host != key.host || receipt.key.core != key.core ||
      receipt.key.revision != key.revision || receipt.key.attempt != key.attempt || receipt.entry <= 0 ||
      !receipt.replyOk || !receipt.started || receipt.invalid || receipt.ended ||
      !capture_->ready(key.attempt, CaptureBridge::Phase::Baseline)) return false;
  const auto epoch = lifetime->originalIdentity().epoch;
  if (!epoch) return false;
  HealthyRestart::PausedHoldBinding binding;
  binding.frame = {session_->launch, key.core, epoch, static_cast<std::uint64_t>(receipt.entry), 2, key.attempt};
  binding.host = key.host; binding.revision = key.revision; binding.attempt = key.attempt;
  binding.grant = player_->m_nativeRestore.grant.id;
  binding.checkpointMs = player_->m_nativeRestore.grant.positionMs;
  binding.deadlineNs = normalDeadline_; binding.claim = claim;
  if (!player_->healthyBeginPausedHold(binding)) return false;
  captureTargetRequested_ = true;
  if (captureSessionReady() && capturePlayerBound() && claim->open() &&
      capture_->request(binding.frame, key.attempt, CaptureBridge::Phase::Target, claim) &&
      captureSessionReady() && claim->open()) return true;
  cancelCapture();
  return false;
}

bool FixtureGraph::captureTargetReady()
{
  if (!capture_ || !captureTargetRequested_ || !captureSessionReady() || !capturePlayerBound() ||
      !player_->restoreOwnsCurrent() || !player_->healthyUnpauseAuthorized(false)) return false;
  const auto key = player_->m_nativeRestore.key;
  return key.host == session_->host && key.core == session_->core && key.revision == session_->intentRevision &&
      key.attempt == session_->attempt && capture_->ready(key.attempt, CaptureBridge::Phase::Target);
}

void FixtureGraph::cancelCapture() noexcept
{
  captureBaselineRequested_ = true;
  captureTargetRequested_ = true;
  if (capture_) capture_->cancel();
}
}
