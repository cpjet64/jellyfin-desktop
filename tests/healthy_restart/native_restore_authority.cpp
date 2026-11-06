#include "PlayerComponent.h"
#include "RestartBroker.h"
#include <chrono>

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

bool PlayerComponent::healthyMarkSeekDispatch()
{
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  if (now <= 0 || m_healthySeekDispatchNs) return false;
  m_healthySeekDispatchNs = now;
  return true;
}
