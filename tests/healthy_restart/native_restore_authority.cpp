#include "PlayerComponent.h"
#include "RestartBroker.h"
#include "capture_capability.h"

// This check is private to the copied diagnostic Player. The graph installs
// the predicate once from the owned module, endpoints, gate and B identity.
bool PlayerComponent::healthyRestoreSubmissionAllowed(bool seeking) const
{
  if (!intentControlThread() || !m_healthyRestoreAuthority || !healthyUnpauseAuthorized(false)) return false;
  const auto claim = m_healthyStopLease;
  const auto key = m_nativeRestore.key;
  const auto bound = m_healthyRestoreAuthorityKey;
  if (!claim || !claim->open() || !restoreOwnsCurrent() ||
      (seeking && (!m_restorePausedHold || !nativeObservationCurrent() || !m_nativeObservation.paused ||
                   m_nativeObservation.positionMs < 0 || m_nativeObservation.positionMs > 100)) || key.host != bound.host ||
      key.core != bound.core || key.revision != bound.revision || key.attempt != bound.attempt) return false;
  try {
    const auto authority = m_healthyRestoreAuthority;
    if (!authority(seeking)) return false;
  } catch (...) { return false; }
  const auto current = m_nativeRestore.key;
  return claim == m_healthyStopLease && claim->open() && restoreOwnsCurrent() &&
      (!seeking || (m_restorePausedHold && nativeObservationCurrent() && m_nativeObservation.paused &&
                    m_nativeObservation.positionMs >= 0 && m_nativeObservation.positionMs <= 100)) &&
      !m_nativeRestore.cancelled && !m_nativeRestore.failed &&
      key.host == current.host && key.core == current.core && key.revision == current.revision &&
      key.attempt == current.attempt && m_restoreGate &&
      m_restoreGate->allowsRestore(m_restoreRun, m_restoreLaunch, m_restoreGrant,
                                   m_restoreRevision, m_restoreDigest, m_restoreGateLast);
}

bool PlayerComponent::healthyAdmitRestoreSubmission(bool seeking)
{
  if (!healthyRestoreSubmissionAllowed(seeking) || !m_healthyRestoreFinalSnapshot ||
      !m_healthyRestoreGeneration || (seeking && m_healthySeekDispatchNs >= 0)) return false;
  const auto key = m_nativeRestore.key;
  const auto claim = m_healthyStopLease;
  const auto capture = seeking ? m_healthyRestoreCapture.lock() : nullptr;
  const auto slot = m_healthyRestoreSlot.lock();
  if (!claim || !slot || (seeking && !capture)) return false;
  qint64 now = -1;
  try {
    const auto snapshot = m_healthyRestoreFinalSnapshot;
    if (!snapshot(now)) return false;
  } catch (...) { return false; }
  if (now < m_restoreGateLast) {
    m_restoreGateClockFailed = true;
    claim->revoke();
    return false;
  }
  const auto current = m_nativeRestore.key;
  if (now < 0 || claim != m_healthyStopLease || !claim->open() || !restoreOwnsCurrent() ||
      m_nativeRestore.cancelled || m_nativeRestore.failed || key.host != current.host || key.core != current.core ||
      key.revision != current.revision || key.attempt != current.attempt || !m_restoreGate ||
      !m_restoreGate->allowsRestore(m_restoreRun, m_restoreLaunch, m_restoreGrant,
                                   m_restoreRevision, m_restoreDigest, now)) return false;
  const HealthyRetirement::RetirementClaim::ArmIdentity identity{key.host, key.core, key.revision, key.attempt};
  const bool admitted = seeking ? capture->admitTarget(m_restoreHoldBinding.frame, key.attempt, claim, identity) :
      slot->admitLoadDispatch(claim, identity);
  if (admitted && seeking) m_healthySeekDispatchNs = now;
  return admitted;
}
