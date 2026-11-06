#include "PlayerComponent.h"
#include "RestartBroker.h"
#include <MpvController>
#include <QScopedValueRollback>
#include <chrono>
#include <cstring>

namespace {
constexpr quint64 cleanupStopTag = quint64{1} << 55;
constexpr quint64 cleanupIdleTag = quint64{1} << 54;
constexpr quint64 cleanupPositionTag = quint64{1} << 53;
qint64 cleanupNow()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
bool sameCleanupKey(PlaybackNativeReceipts::Key a, PlaybackNativeReceipts::Key b)
{
  return a.host == b.host && a.core == b.core && a.revision == b.revision && a.attempt == b.attempt;
}
bool validFailure(HealthyRestart::FailureCause failure)
{
  using Cause = HealthyRestart::FailureCause;
  return failure == Cause::ProtocolViolation || failure == Cause::EndpointLost ||
      failure == Cause::InitializationFailure || failure == Cause::OwnershipLost || failure == Cause::Deadline;
}
}

bool PlayerComponent::healthyBindRestoreCleanupGate(std::shared_ptr<const FixtureRestart::ChildGate> gate, qint64 deadlineNs)
{
  if (!intentControlThread() || !gate || gate->canceled() || m_restoreCleanupGate || m_nativeRestore.armed || m_nativeRestore.loadAttempted ||
      m_restoreCleanup.claimed) return false;
  qint64 now;
  try { now = m_healthyStopClock ? m_healthyStopClock() : cleanupNow(); } catch (...) { return false; }
  if (now < 0 || deadlineNs <= now || gate->canceled() || m_restoreCleanupGate ||
      m_nativeRestore.armed || m_nativeRestore.loadAttempted || m_restoreCleanup.claimed) return false;
  m_restoreCleanup.deadlineNs = deadlineNs;
  m_restoreCleanup.lastNs = now;
  m_restoreCleanupGate = std::move(gate);
  return true;
}

bool PlayerComponent::restoreCleanupTimeValid()
{
  try {
    const auto now = m_healthyStopClock ? m_healthyStopClock() : cleanupNow();
    if (now < 0 || now < m_restoreCleanup.lastNs || now >= m_restoreCleanup.deadlineNs) return false;
    m_restoreCleanup.lastNs = now;
    return true;
  } catch (...) { return false; }
}

bool PlayerComponent::restoreCleanupOwnsCurrent() const
{
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto intent = m_playbackIntent.snapshot();
  const auto receipt = m_nativeReceipts.snapshot();
  const auto& state = m_restoreCleanup;
  return state.key.host && state.key.host == m_receiptHost && state.key.core == m_mpvGeneration &&
      state.key.revision == intent.revision && state.epoch == intent.epoch && state.request == intent.request &&
      sameCleanupKey(state.key, m_nativeRestore.key) &&
      (!receipt.key.attempt || sameCleanupKey(state.key, receipt.key)) &&
      (m_restoreDispatch || (m_mpv && m_mpv->mpv()));
#else
  return false;
#endif
}

bool PlayerComponent::restoreCleanupPriorSettled() const
{
  const auto receipt = m_nativeReceipts.snapshot();
  const bool load = !m_nativeRestore.loadAttempted || (receipt.submissionObserved && (!receipt.submitted || receipt.reply));
  const bool seek = !m_nativeRestore.seekAttempted ||
      (m_nativeRestore.seekSubmissionKnown && (!m_nativeRestore.seekAccepted || m_nativeRestore.seekReply));
  const bool unpause = !m_restoreUnpause.attempted ||
      (m_restoreUnpause.submissionKnown && (!m_restoreUnpause.submitted || m_restoreUnpause.replied));
  return load && seek && unpause && !m_nativeObservation.pending;
}

bool PlayerComponent::beginNativeRestoreCleanup(std::shared_ptr<const FixtureRestart::ChildGate> gate,
    std::optional<HealthyRestart::VerifiedCleanupCancel> cancel, qint64 deadlineNs, HealthyRestart::FailureCause failure)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  using Origin = HealthyRestart::RestoreCleanupState::Origin;
  const bool internal = failure != HealthyRestart::FailureCause::None;
  if (!intentControlThread() || !gate || gate != m_restoreCleanupGate || !gate->canceled() ||
      !m_nativeRestore.armed || deadlineNs != m_restoreCleanup.deadlineNs ||
      (internal ? (!validFailure(failure) || bool(cancel)) :
       (!cancel || !cancel->sequence || !cancel->revision || !gate->matchesVerifiedCancel(cancel->sequence, cancel->revision)))) return false;
  auto& state = m_restoreCleanup;
  if (state.claimed) {
    if (!internal || deadlineNs != state.deadlineNs || !restoreCleanupOwnsCurrent()) return false;
    state.origin = Origin::InternalFailure;
    if (state.firstFailure == HealthyRestart::FailureCause::None) state.firstFailure = failure;
    return m_healthyStopLease && m_healthyStopLease->recognizeCleanup(m_healthyRendererGeneration);
  }
  const auto intent = m_playbackIntent.snapshot();
  const auto key = m_nativeRestore.key;
  const auto receipt = m_nativeReceipts.snapshot();
  if (!key.host || key.host != m_receiptHost || !key.core || key.core != m_mpvGeneration ||
      !key.revision || key.revision != intent.revision || !key.attempt || !intent.epoch || !intent.request ||
      (receipt.key.attempt && !sameCleanupKey(key, receipt.key)) ||
      (!m_restoreDispatch && (!m_mpv || !m_mpv->mpv())) || !m_healthyStopLease ||
      !m_healthyRendererGeneration || m_healthyStopLease->generation() != m_healthyRendererGeneration ||
      !m_healthyStopLease->enterCleanup(m_healthyRendererGeneration)) return false;
  bool stopAdmitted = false;
  if (m_nativeRestore.loadAttempted) {
    if (m_healthyStopLease->admitCleanupStop(m_healthyRendererGeneration,
        {key.host, key.core, key.revision, key.attempt}) !=
        HealthyRetirement::RetirementClaim::Admission::New) return false;
    stopAdmitted = true;
  }
  state.claimed = true;
  state.origin = internal ? Origin::InternalFailure : Origin::AuthenticatedCancel;
  state.firstFailure = failure;
  state.cancel = internal ? std::nullopt : cancel;
  state.deadlineNs = deadlineNs;
  state.stopAdmitted = stopAdmitted;
  state.status = RestoreCleanupStatus::Failed;
  state.key = key; state.epoch = intent.epoch; state.request = intent.request;
  m_nativeRestore.cancelled = true;
  if (!internal) m_healthyStopLease->revoke();
  m_healthyStopAdmissionClosed.store(true);
  m_restoreDisplayTimer.stop();
  m_reloadAudioTimer.stop();
  m_healthyStopQueueEmpty = false;
  if (!restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent()) return false;
  if (!m_nativeRestore.loadAttempted) {
    if (receipt.key.attempt || receipt.consumed || m_lastReceiptAttempt || m_nativeRestore.seekAttempted || m_restoreUnpause.attempted ||
        m_nativeObservation.pending) return false;
    state.noLoadCandidate = true;
    state.status = RestoreCleanupStatus::Pending;
    return true;
  }
  if (!receipt.key.attempt || !sameCleanupKey(state.key, receipt.key) ||
      !receipt.submissionObserved) return false;
  state.entry = receipt.entry;
  if (receipt.started) state.startedEntry = receipt.entry;
  state.status = RestoreCleanupStatus::Pending;
  return true;
#else
  Q_UNUSED(gate); Q_UNUSED(cancel); Q_UNUSED(deadlineNs); Q_UNUSED(failure); return false;
#endif
}

void PlayerComponent::submitRestoreCleanupStop()
{
  auto& state = m_restoreCleanup;
  if (state.stopIssued || state.status != RestoreCleanupStatus::Pending) return;
  if (!state.stopAdmitted || !m_healthyStopLease || !restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent() ||
      state.status != RestoreCleanupStatus::Pending) {
    state.status = RestoreCleanupStatus::Failed; return;
  }
  if (!restoreCleanupPriorSettled()) return;
  const char* args[] = {"stop", nullptr};
  state.stopIssued = true;
  int result = MPV_ERROR_UNINITIALIZED;
  try {
    if (m_restoreDispatch) result = m_restoreDispatch(cleanupStopTag | state.key.attempt, args);
    else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), cleanupStopTag | state.key.attempt, args);
  } catch (...) {}
  state.stopKnown = true; state.stopSubmitted = result >= 0; state.stopError = result;
  if (state.stopSubmitted && !m_healthyStopLease->recordStopSubmitted(m_healthyRendererGeneration))
    state.status = RestoreCleanupStatus::Failed;
  if (result < 0 || (state.stopReply && !state.stopOk) || !restoreCleanupOwnsCurrent() ||
      !restoreCleanupTimeValid()) state.status = RestoreCleanupStatus::Failed;
}

void PlayerComponent::requestRestoreCleanupProperty(bool position)
{
  auto& state = m_restoreCleanup;
  auto& property = position ? state.position : state.idle;
  if (property.requested || state.status != RestoreCleanupStatus::Pending) return;
  if (!restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent() || !m_healthyStopLease ||
      m_healthyStopLease->contained() || state.status != RestoreCleanupStatus::Pending) {
    state.status = RestoreCleanupStatus::Failed; return;
  }
  property.requested = true;
  m_healthyStopQueueEmpty = false;
  const char* args[] = {position ? "playlist-pos" : "idle-active", nullptr};
  const auto id = (position ? cleanupPositionTag : cleanupIdleTag) | state.key.attempt;
  int result = MPV_ERROR_UNINITIALIZED;
  try {
    if (m_healthyStopDispatch) result = m_healthyStopDispatch(id, args, true);
    else if (m_mpv && m_mpv->mpv())
      result = mpv_get_property_async(m_mpv->mpv(), id, args[0], position ? MPV_FORMAT_INT64 : MPV_FORMAT_FLAG);
  } catch (...) {}
  property.known = true; property.submitted = result >= 0;
  if (result < 0 || !restoreCleanupOwnsCurrent() || !restoreCleanupTimeValid()) state.status = RestoreCleanupStatus::Failed;
}

PlayerComponent::RestoreCleanupStatus PlayerComponent::pollNativeRestoreCancellation()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  auto& state = m_restoreCleanup;
  if (!intentControlThread()) return RestoreCleanupStatus::Failed;
  if (state.status == RestoreCleanupStatus::Unstarted || state.status == RestoreCleanupStatus::Failed) return state.status;
  if (state.polling) return RestoreCleanupStatus::Pending;
  QScopedValueRollback<bool> polling(state.polling, true);
  if (!restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent()) return state.status = RestoreCleanupStatus::Failed;
  if (m_healthyStopLease && m_healthyStopLease->contained() && !state.stopAdmitted)
    return state.status = RestoreCleanupStatus::Failed;
  if (state.status == RestoreCleanupStatus::NoLoad || state.status == RestoreCleanupStatus::Stopped) {
    if (!m_healthyStopLease || m_healthyStopLease->contained() ||
        !m_healthyStopLease->matchesArm(m_healthyRendererGeneration,
            {state.key.host, state.key.core, state.key.revision, state.key.attempt}))
      state.status = RestoreCleanupStatus::Failed;
    return state.status;
  }
  if ((m_mpv && m_mpv->mpv()) || m_healthyStopEventWait) {
    if (m_healthyStopDraining) return state.status;
    handleMpvEvents();
    if (state.status != RestoreCleanupStatus::Pending || !m_healthyStopQueueEmpty) return state.status;
    if (!restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent()) return state.status = RestoreCleanupStatus::Failed;
  } else if (m_restoreDispatch) m_healthyStopQueueEmpty = true;
  else return state.status = RestoreCleanupStatus::Failed;
  if (state.noLoadCandidate) {
    const auto receipt = m_nativeReceipts.snapshot();
    if (m_nativeRestore.loadAttempted || receipt.key.attempt || receipt.consumed || m_lastReceiptAttempt || !restoreCleanupPriorSettled())
      return state.status = RestoreCleanupStatus::Failed;
    requestRestoreCleanupProperty(false);
    if (state.status != RestoreCleanupStatus::Pending) return state.status;
    requestRestoreCleanupProperty(true);
    if (state.status != RestoreCleanupStatus::Pending) return state.status;
    const auto finalReceipt = m_nativeReceipts.snapshot();
    if (state.propertiesReady() && m_healthyStopQueueEmpty && !m_nativeRestore.loadAttempted &&
        !finalReceipt.key.attempt && !finalReceipt.consumed &&
        !m_lastReceiptAttempt &&
        restoreCleanupPriorSettled() && restoreCleanupTimeValid() &&
        restoreCleanupOwnsCurrent() && m_healthyStopLease && !m_healthyStopLease->contained() &&
        m_healthyStopLease->matchesArm(m_healthyRendererGeneration,
            {state.key.host, state.key.core, state.key.revision, state.key.attempt})) {
      if (state.origin == HealthyRestart::RestoreCleanupState::Origin::InternalFailure &&
          !m_healthyStopLease->qualifyCleanup(m_healthyRendererGeneration,
              {state.key.host, state.key.core, state.key.revision, state.key.attempt})) state.status = RestoreCleanupStatus::Failed;
      else state.status = RestoreCleanupStatus::NoLoad;
    }
    return state.status;
  }
  if (!state.stopIssued) submitRestoreCleanupStop();
  if (state.status != RestoreCleanupStatus::Pending) return state.status;
  if (m_healthyStopLease && m_healthyStopLease->contained() && state.stopKnown &&
      (!state.stopSubmitted || state.stopReply)) return state.status = RestoreCleanupStatus::Failed;
  if (!state.stopKnown || !state.stopSubmitted || !state.stopReply || !state.stopOk || !restoreCleanupPriorSettled())
    return state.status;
  const auto receipt = m_nativeReceipts.snapshot();
  if (receipt.invalid) return state.status = RestoreCleanupStatus::Failed;
  if (!receipt.replyOk || receipt.entry <= 0) return state.status;
  if (state.entry && state.entry != receipt.entry) return state.status = RestoreCleanupStatus::Failed;
  state.entry = receipt.entry;
  if (receipt.started) {
    if (!state.startedEntry) state.startedEntry = receipt.entry;
    if (state.startedEntry != receipt.entry || (state.endSeen && state.endEntry != receipt.entry))
      return state.status = RestoreCleanupStatus::Failed;
    if (!state.endSeen || !receipt.ended || !receipt.settled) return state.status;
  } else if (state.startedEntry || state.endSeen || receipt.ended) return state.status = RestoreCleanupStatus::Failed;
  requestRestoreCleanupProperty(false);
  if (state.status != RestoreCleanupStatus::Pending) return state.status;
  requestRestoreCleanupProperty(true);
  if (state.status != RestoreCleanupStatus::Pending) return state.status;
  const auto finalReceipt = m_nativeReceipts.snapshot();
  if (state.propertiesReady() && m_healthyStopQueueEmpty && state.terminalReceiptMatches(finalReceipt, receipt.started) &&
      restoreCleanupPriorSettled() && restoreCleanupTimeValid() && restoreCleanupOwnsCurrent() &&
      m_healthyStopLease && !m_healthyStopLease->contained() &&
      m_healthyStopLease->matchesArm(m_healthyRendererGeneration,
          {state.key.host, state.key.core, state.key.revision, state.key.attempt})) {
    if (receipt.started) {
      if (state.origin == HealthyRestart::RestoreCleanupState::Origin::InternalFailure &&
          !m_healthyStopLease->qualifyCleanup(m_healthyRendererGeneration,
              {state.key.host, state.key.core, state.key.revision, state.key.attempt})) state.status = RestoreCleanupStatus::Failed;
      else state.status = RestoreCleanupStatus::Stopped;
    } else state.noStartTerminalObserved = true; // No qualified barrier excludes later activation.
  }
  return state.status;
#else
  return RestoreCleanupStatus::Failed;
#endif
}

bool PlayerComponent::recordRestoreCleanupStopReply(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  auto& state = m_restoreCleanup;
  if (event->event_id != MPV_EVENT_COMMAND_REPLY || event->reply_userdata != (cleanupStopTag | state.key.attempt)) return false;
  m_healthyStopQueueEmpty = false;
  if (!state.stopAdmitted || !state.stopIssued || state.stopReply || (state.stopKnown && !state.stopSubmitted) ||
      state.key.host != m_receiptHost || state.key.core != m_mpvGeneration || !m_healthyStopLease) {
    state.status = RestoreCleanupStatus::Failed; return true;
  }
  state.stopReply = true; state.replyError = event->error; state.stopOk = event->error >= 0;
  if (!m_healthyStopLease->recordStopCompleted(m_healthyRendererGeneration) || !state.stopOk ||
      !restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent()) state.status = RestoreCleanupStatus::Failed;
  return true;
#else
  Q_UNUSED(event); return false;
#endif
}

bool PlayerComponent::observeNativeRestoreCleanup(const mpv_event* event)
{
#ifdef MPVQT_EXTERNAL_EVENTS
  auto& state = m_restoreCleanup;
  if (!event || !state.claimed || state.status == RestoreCleanupStatus::Unstarted) return false;
  if (recordRestoreCleanupStopReply(event) || state.status == RestoreCleanupStatus::Failed) return false;
  if (!restoreCleanupTimeValid() || !restoreCleanupOwnsCurrent()) { state.status = RestoreCleanupStatus::Failed; return false; }
  if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN) {
    state.status = RestoreCleanupStatus::Failed; return false;
  }
  if (state.noLoadCandidate &&
      (event->event_id == MPV_EVENT_START_FILE || event->event_id == MPV_EVENT_END_FILE)) {
    state.status = RestoreCleanupStatus::Failed; return false;
  }
  if (event->event_id == MPV_EVENT_START_FILE) {
    const auto* start = static_cast<const mpv_event_start_file*>(event->data);
    if (!start || start->playlist_entry_id <= 0 || state.startedEntry || state.idle.requested || state.position.requested ||
        (state.entry && state.entry != start->playlist_entry_id))
      state.status = RestoreCleanupStatus::Failed;
    else state.startedEntry = start->playlist_entry_id;
    return false;
  }
  if (event->event_id == MPV_EVENT_END_FILE) {
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    const auto receipt = m_nativeReceipts.snapshot();
    if (!state.startedEntry && sameCleanupKey(receipt.key, state.key) && receipt.started && receipt.entry > 0)
      state.startedEntry = receipt.entry;
    const auto expected = state.entry ? state.entry : state.startedEntry;
    if (!end || !state.stopIssued || end->playlist_entry_id <= 0 || (expected && end->playlist_entry_id != expected) ||
        state.endSeen || state.idle.requested || state.position.requested ||
        end->reason != MPV_END_FILE_REASON_STOP) { state.status = RestoreCleanupStatus::Failed; return false; }
    state.endSeen = true; state.endEntry = end->playlist_entry_id; state.endReason = int(end->reason);
    return true;
  }
  if (event->event_id != MPV_EVENT_GET_PROPERTY_REPLY) return false;
  const bool position = event->reply_userdata == (cleanupPositionTag | state.key.attempt);
  if (!position && event->reply_userdata != (cleanupIdleTag | state.key.attempt)) return false;
  m_healthyStopQueueEmpty = false;
  auto& property = position ? state.position : state.idle;
  if (!property.requested || property.reply || (property.known && !property.submitted)) {
    state.status = RestoreCleanupStatus::Failed; return false;
  }
  property.reply = true;
  const auto* data = static_cast<const mpv_event_property*>(event->data);
  property.valid = event->error >= 0 && data && data->name && data->data &&
      !std::strcmp(data->name, position ? "playlist-pos" : "idle-active") &&
      data->format == (position ? MPV_FORMAT_INT64 : MPV_FORMAT_FLAG);
  if (!property.valid) { state.status = RestoreCleanupStatus::Failed; return false; }
  property.value = position ? *static_cast<const int64_t*>(data->data) : *static_cast<const int*>(data->data);
  return false;
#else
  Q_UNUSED(event); return false;
#endif
}
