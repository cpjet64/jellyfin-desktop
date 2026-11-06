#include "PlayerComponent.h"
#include "RestartBroker.h"
#include "frame_capture.h"
#include <MpvController>
#include <chrono>
#include <utility>

namespace {
constexpr quint64 unpauseTag = quint64{1} << 56;
qint64 unpauseNowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
}

bool PlayerComponent::healthyBindRestoreGate(std::shared_ptr<const FixtureRestart::ChildGate> gate,
    quint64 run, quint64 launch, quint64 grant, quint64 revision, QByteArray digest,
    std::function<qint64()> clock)
{
  if (!intentControlThread() || m_restoreGate || !gate || !clock || !run || !launch || !grant || !revision ||
      grant != m_nativeRestore.grant.id || revision != m_nativeRestore.grant.parentRevision ||
      !restoreOwnsCurrent() || !m_healthyStopLease || !m_healthyStopLease->open()) return false;
  try {
    const auto now = clock();
    const auto* accepted = gate->acceptedGrant();
    if (now < 0 || !gate->allowsRestore(run, launch, grant, revision, digest, now) ||
        !accepted || accepted->positionMs != m_nativeRestore.grant.positionMs ||
        accepted->desiredPaused != m_nativeRestore.grant.desiredPaused ||
        !restoreOwnsCurrent() || !m_healthyStopLease || !m_healthyStopLease->open() ||
        grant != m_nativeRestore.grant.id || revision != m_nativeRestore.grant.parentRevision) return false;
    m_restoreGate = std::move(gate);
    m_restoreRun = run; m_restoreLaunch = launch; m_restoreGrant = grant; m_restoreRevision = revision;
    m_restoreDigest = std::move(digest); m_restoreGateClock = std::move(clock); m_restoreGateLast = now;
    return true;
  } catch (...) { return false; }
}

bool PlayerComponent::healthyUnpauseAuthorized(bool requireHold) const
{
  if (!intentControlThread() || !m_restoreGate || !m_restoreGateClock || m_restoreGateClockFailed ||
      !m_healthyStopLease || !m_healthyStopLease->open() || !restoreOwnsCurrent() ||
      m_nativeRestore.cancelled || m_nativeRestore.failed || fixture_restart::captureOverflowed()) return false;
  try {
    const auto now = m_restoreGateClock();
    if (now < 0 || now < m_restoreGateLast) { m_restoreGateClockFailed = true; return false; }
    m_restoreGateLast = now;
    if (!m_restoreGate->allowsRestore(m_restoreRun, m_restoreLaunch, m_restoreGrant,
                                     m_restoreRevision, m_restoreDigest, now)) return false;
    const auto* accepted = m_restoreGate->acceptedGrant();
    if (!accepted || accepted->positionMs != m_nativeRestore.grant.positionMs ||
        accepted->desiredPaused != m_nativeRestore.grant.desiredPaused) return false;
    if (!restoreOwnsCurrent() || !m_healthyStopLease || !m_healthyStopLease->open() ||
        m_nativeRestore.cancelled || m_nativeRestore.failed) return false;
    if (m_restorePausedHold) {
      const auto key = m_nativeRestore.key;
      const auto receipt = m_nativeReceipts.snapshot();
      const auto nowNs = unpauseNowNs();
      if (nowNs < 0 || nowNs >= m_restoreHoldBinding.deadlineNs || receipt.entry <= 0 ||
          m_restoreHoldBinding.host != key.host || m_restoreHoldBinding.frame.core != key.core ||
          m_restoreHoldBinding.revision != key.revision || m_restoreHoldBinding.attempt != key.attempt ||
          m_restoreHoldBinding.frame.entry != static_cast<quint64>(receipt.entry) ||
          m_restoreHoldBinding.grant != m_nativeRestore.grant.id || m_restoreHoldBinding.claim != m_healthyStopLease)
        return false;
    }
    if (m_restoreUnpause.attempted && (!m_restorePausedProof || !m_restorePausedProof->retained())) return false;
    return !requireHold || (m_restorePausedProof && m_restorePausedProof->matches(m_restoreHoldBinding, unpauseNowNs()));
  } catch (...) { m_restoreGateClockFailed = true; return false; }
}

bool PlayerComponent::healthyBeginPausedHold(HealthyRestart::PausedHoldBinding binding)
{
  if (!healthyUnpauseAuthorized(false) || m_restorePausedHold || m_restorePausedProof ||
      m_nativeRestore.seekAttempted || !nativeObservationCurrent() || !m_nativeObservation.paused) return false;
  const auto receipt = m_nativeReceipts.snapshot();
  const auto key = m_nativeRestore.key;
  if (binding.host != key.host || binding.frame.core != key.core || binding.revision != key.revision ||
      binding.attempt != key.attempt || binding.grant != m_nativeRestore.grant.id ||
      binding.frame.entry != static_cast<quint64>(receipt.entry) || receipt.entry <= 0 ||
      binding.checkpointMs != m_nativeRestore.grant.positionMs || binding.claim != m_healthyStopLease) return false;
  try {
    fixture_restart::Capture baseline;
    for (const auto& record : fixture_restart::takeCaptures()) {
      if (record && record->key.process == binding.frame.process && record->key.core == binding.frame.core &&
          record->key.context == binding.frame.context && record->key.entry == binding.frame.entry)
        baseline = record;
    }
    if (fixture_restart::captureOverflowed() || !baseline) return false;
    auto hold = std::unique_ptr<HealthyRestart::PausedHold>(new HealthyRestart::PausedHold(binding));
    if (!hold->baseline(std::move(baseline)) ||
        !hold->arm(fixture_restart::currentRenderSerial(), unpauseNowNs())) return false;
    m_restoreHoldBinding = std::move(binding); m_restorePausedHold = std::move(hold);
    return true;
  } catch (...) { return false; }
}

bool PlayerComponent::healthyCapturePausedHold()
{
  if (!intentControlThread() || !m_restorePausedHold || !healthyUnpauseAuthorized(false)) return false;
  for (auto& capture : fixture_restart::takeCaptures()) m_restorePausedHold->capture(std::move(capture));
  if (fixture_restart::captureOverflowed()) { m_restorePausedHold->invalidate(); return false; }
  return true;
}

bool PlayerComponent::healthyObservePausedHold()
{
  if (!m_restorePausedHold || !healthyUnpauseAuthorized(false) || !nativePostSeekObservationCurrent()) return false;
  return m_restorePausedHold->observe(m_nativeObservation.id, m_nativeObservation.requestedNs,
                                     m_nativeObservation.receivedNs, m_nativeObservation.positionMs);
}

bool PlayerComponent::healthyFinishPausedHold()
{
  if (!m_restorePausedHold || m_restorePausedProof || !healthyUnpauseAuthorized(false) ||
      !nativePostSeekObservationCurrent()) return false;
  m_restorePausedProof = m_restorePausedHold->finish(unpauseNowNs());
  return bool(m_restorePausedProof);
}

bool PlayerComponent::healthyUnpausePrerequisites() const
{
#ifdef MPVQT_EXTERNAL_EVENTS
  return intentControlThread() && !m_healthyStopAdmissionClosed.load() && healthyUnpauseAuthorized(true) &&
      restorePostSeekReady() && nativePostSeekObservationCurrent() && !m_nativeObservation.pending &&
      !m_nativeRestore.grant.desiredPaused;
#else
  return false;
#endif
}

bool PlayerComponent::nativeRestoreUnpauseReady() const
{
  return !m_restoreUnpause.attempted && healthyUnpausePrerequisites();
}

bool PlayerComponent::dispatchNativeRestoreUnpause()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!intentControlThread() || m_restoreUnpause.attempted) return false;
  m_restoreUnpause.attempted = true;
  if (!healthyUnpausePrerequisites()) { m_restoreUnpause.failed = true; return false; }
  const auto key = m_nativeRestore.key;
  m_restoreUnpause.previousObservation = m_nativeObservation.id;
  m_nativeObservation.valid = false;
  const char* args[] = {"set", "pause", "no", nullptr};
  int result = MPV_ERROR_UNINITIALIZED;
  if (m_restoreDispatch) result = m_restoreDispatch(unpauseTag | key.attempt, args);
  else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), unpauseTag | key.attempt, args);
  m_restoreUnpause.submissionKnown = true;
  m_restoreUnpause.submissionNs = unpauseNowNs();
  m_restoreUnpause.submitted = result >= 0;
  m_restoreUnpause.submissionError = result;
  if (result < 0 || !restoreOwnsCurrent() || !healthyUnpauseAuthorized(false) ||
      m_nativeRestore.cancelled || m_nativeRestore.failed) m_restoreUnpause.failed = true;
  return !m_restoreUnpause.failed;
#else
  return false;
#endif
}

void PlayerComponent::observeNativeRestoreUnpause(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  if (!event || event->event_id != MPV_EVENT_COMMAND_REPLY || !m_restoreUnpause.attempted ||
      event->reply_userdata != (unpauseTag | m_nativeRestore.key.attempt)) return;
  if (m_restoreUnpause.replied) { m_restoreUnpause.failed = true; return; }
  m_restoreUnpause.replied = true;
  m_restoreUnpause.replyError = event->error;
  m_restoreUnpause.replyOk = event->error >= 0;
  m_restoreUnpause.replyNs = unpauseNowNs();
  if (!m_restoreUnpause.replyOk || !restoreOwnsCurrent() || !healthyUnpauseAuthorized(false) ||
      m_nativeRestore.cancelled || m_nativeRestore.failed) m_restoreUnpause.failed = true;
#else
  Q_UNUSED(event);
#endif
}

bool PlayerComponent::nativeRestoreUnpauseComplete() const
{
#ifdef MPVQT_EXTERNAL_EVENTS
  return intentControlThread() && healthyUnpauseAuthorized(false) && restorePostSeekReady() &&
      !m_restoreUnpause.failed && m_restoreUnpause.attempted && m_restoreUnpause.submissionKnown &&
      m_restoreUnpause.submitted && m_restoreUnpause.replied && m_restoreUnpause.replyOk &&
      m_restoreUnpause.replyNs >= 0 && m_restoreUnpause.submissionNs >= 0 &&
      m_nativeObservation.id > m_restoreUnpause.previousObservation &&
      m_nativeObservation.requestedNs >= m_restoreUnpause.replyNs &&
      m_nativeObservation.requestedNs >= m_restoreUnpause.submissionNs && !m_nativeObservation.paused &&
      nativeObservationCurrent();
#else
  return false;
#endif
}
