#include "PlayerComponent.h"
#include <MpvController>
#include <chrono>
#include <cstring>

namespace {
using Stop = HealthyRestart::AsyncStop;
constexpr quint64 stopTag = quint64{1} << 60;
constexpr quint64 beforeIdleTag = quint64{1} << 59;
constexpr quint64 beforePositionTag = quint64{1} << 58;
constexpr quint64 afterIdleTag = quint64{1} << 57;
qint64 nowNs()
{
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
}
quint64 queryTag(Stop::Query query)
{
  switch (query) {
  case Stop::Query::BeforeIdle: return beforeIdleTag;
  case Stop::Query::BeforePosition: return beforePositionTag;
  case Stop::Query::AfterIdle: return afterIdleTag;
  }
  return 0;
}
}

bool PlayerComponent::healthyStopCurrent() const
{
  const auto key = m_healthyStop.facts().key;
  const auto intent = m_playbackIntent.snapshot();
  return m_healthyStopLease && m_healthyStopLease->eligible() &&
      (m_healthyStopDispatch || (m_mpv && m_mpv->mpv())) && key.host == m_receiptHost && key.core == m_mpvGeneration &&
      key.epoch == intent.epoch && key.request == intent.request && key.revision == intent.revision &&
      intent.desired == (key.paused ? PlaybackIntent::Desired::Paused : PlaybackIntent::Desired::Playing);
}

bool PlayerComponent::healthyInstallRetirementClaim(std::shared_ptr<HealthyRetirement::RetirementClaim> claim, quint64 generation)
{
  if (!intentControlThread() || m_healthyStopAdmissionClosed.load() || m_healthyStopLease || !claim ||
      !generation || claim->generation() != generation || !claim->open()) return false;
  const auto intent = m_playbackIntent.snapshot();
  if (!intent.epoch || !intent.request || !intent.revision ||
      (intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused)) return false;
  m_healthyRendererGeneration = generation;
  m_healthyStopLease = std::move(claim);
  m_playbackIntent.watchRetirement(m_healthyStopLease);
  return true;
}

bool PlayerComponent::beginHealthyReplacementStop(quint64 host, quint64 attempt, qint64 deadlineNs)
{
  if (!intentControlThread() || m_healthyStopAdmissionClosed.load() ||
      !m_healthyStopLease || !m_healthyStopLease->open() || !m_healthyRendererGeneration ||
      m_healthyStopLease->generation() != m_healthyRendererGeneration ||
      (!m_healthyStopDispatch && (!m_mpv || !m_mpv->mpv())) || !host || host != m_receiptHost ||
      !m_mpvGeneration || !attempt || attempt > PlaybackIntent::MaxSafeInteger ||
      m_nativeObservation.pending || m_nativeRestore.seekPending) return false;
  const auto intent = m_playbackIntent.snapshot();
  if (!intent.epoch || !intent.request || !intent.revision ||
      (intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused)) return false;
  const auto receipt = m_nativeReceipts.snapshot();
  qint64 entry = 0;
  if (receipt.key.attempt) {
    if (receipt.key.host != host || receipt.key.core != m_mpvGeneration || receipt.key.revision != intent.revision ||
        receipt.key.attempt != attempt || receipt.invalid || !receipt.consumed || !receipt.submissionObserved || !receipt.submitted)
      return false;
    if (receipt.replyOk && receipt.started && !receipt.ended && receipt.entry > 0) entry = receipt.entry;
    else if (!receipt.settled || !receipt.ended) return false;
  }
  const Stop::Key key{host, m_mpvGeneration, intent.epoch, intent.request, intent.revision,
                      attempt, entry, intent.desired == PlaybackIntent::Desired::Paused, Stop::Origin::InternalReplacement};
  const auto clock = m_healthyStopClock ? m_healthyStopClock : std::function<qint64()>{nowNs};
  m_healthyStopAdmissionClosed.store(true);
  if (!m_healthyStop.begin(key, deadlineNs, clock)) { m_healthyStopLease->revoke(); return false; }
  m_restoreDisplayTimer.stop();
  m_reloadAudioTimer.stop();
  invalidateNativeReceipt();
  if ((m_mpv && m_mpv->mpv()) || m_healthyStopEventWait) handleMpvEvents();
  else m_healthyStopQueueEmpty = true; // Injected dispatch has no native queue.
  if (!m_healthyStopQueueEmpty) return m_healthyStop.status() == Stop::Status::Pending;
  pumpHealthyReplacementStop();
  if (m_healthyStop.status() == Stop::Status::Failed) { m_healthyStopLease->revoke(); return false; }
  return true;
}

void PlayerComponent::submitHealthyStopQuery(Stop::Query query)
{
  if (!m_healthyStop.requestQuery(query)) { failHealthyReplacementStop(); return; }
  const auto key = m_healthyStop.facts().key;
  const bool position = query == Stop::Query::BeforePosition;
  const char* args[] = {position ? "playlist-pos" : "idle-active", nullptr};
  const auto id = queryTag(query) | key.attempt;
  int result = MPV_ERROR_UNINITIALIZED;
  if (m_healthyStopDispatch) result = m_healthyStopDispatch(id, args, true);
  else if (m_mpv && m_mpv->mpv())
    result = mpv_get_property_async(m_mpv->mpv(), id, args[0], position ? MPV_FORMAT_INT64 : MPV_FORMAT_FLAG);
  m_healthyStop.querySubmitted(query, result);
  if (!healthyStopCurrent()) failHealthyReplacementStop();
  if (m_healthyStop.status() == Stop::Status::Failed) m_healthyStopLease->revoke();
}

void PlayerComponent::submitHealthyStopCommand()
{
  if (!m_healthyStop.requestStop()) { failHealthyReplacementStop(); return; }
  const auto key = m_healthyStop.facts().key;
  const char* args[] = {"stop", nullptr};
  int result = MPV_ERROR_UNINITIALIZED;
  if (m_healthyStopDispatch) result = m_healthyStopDispatch(stopTag | key.attempt, args, false);
  else if (m_mpv && m_mpv->mpv()) result = mpv_command_async(m_mpv->mpv(), stopTag | key.attempt, args);
  m_healthyStop.stopSubmitted(result);
  reconcileHealthyStopFailure();
  if (!healthyStopCurrent()) failHealthyReplacementStop();
  if (m_healthyStop.status() == Stop::Status::Failed) m_healthyStopLease->revoke();
}

void PlayerComponent::reconcileHealthyStopFailure()
{
  const auto facts = m_healthyStop.facts();
  const auto intent = m_playbackIntent.snapshot();
  if (facts.endSeen && ((facts.submissionKnown && !facts.submitted) || (facts.replied && !facts.replyOk)) &&
      facts.key.epoch == intent.epoch && facts.key.request == intent.request && facts.key.revision == intent.revision)
    m_playbackIntent.invalidate(PlaybackIntent::Reason::UnattributedEvent);
}

void PlayerComponent::pumpHealthyReplacementStop()
{
  if (m_healthyStop.status() != Stop::Status::Pending) return;
  if (!healthyStopCurrent()) { failHealthyReplacementStop(); return; }
  auto facts = m_healthyStop.facts();
  if (!facts.key.entry) {
    if (!facts.queries[static_cast<size_t>(Stop::Query::BeforeIdle)].requested)
      submitHealthyStopQuery(Stop::Query::BeforeIdle);
    facts = m_healthyStop.facts();
    if (facts.failed) return;
    if (!facts.queries[static_cast<size_t>(Stop::Query::BeforePosition)].requested)
      submitHealthyStopQuery(Stop::Query::BeforePosition);
  }
  if (m_healthyStop.readyToSubmitStop()) submitHealthyStopCommand();
  facts = m_healthyStop.facts();
  if (m_healthyStop.readyForIdleQuery() && !facts.queries[static_cast<size_t>(Stop::Query::AfterIdle)].requested)
    submitHealthyStopQuery(Stop::Query::AfterIdle);
}

Stop::Status PlayerComponent::pollHealthyReplacementStop()
{
  if (!intentControlThread()) return Stop::Status::Failed;
  if (m_healthyStop.status() != Stop::Status::Unstarted && !healthyStopCurrent()) failHealthyReplacementStop();
  const auto initial = m_healthyStop.status();
  if ((initial == Stop::Status::Pending || initial == Stop::Status::Complete) &&
      ((m_mpv && m_mpv->mpv()) || m_healthyStopEventWait)) {
    if (m_healthyStopDraining) return Stop::Status::Pending;
    handleMpvEvents();
    if (!m_healthyStopQueueEmpty) return m_healthyStop.status();
  }
  pumpHealthyReplacementStop();
  const auto status = m_healthyStop.status();
  if (status == Stop::Status::Failed && m_healthyStopLease) m_healthyStopLease->revoke();
  return status;
}

std::optional<Stop::Key> PlayerComponent::healthyCompletedStop()
{
  if (pollHealthyReplacementStop() != Stop::Status::Complete) return std::nullopt;
  const auto facts = m_healthyStop.facts();
  if (facts.failed || facts.cancelled || !m_healthyStopLease || !m_healthyStopLease->eligible() || !healthyStopCurrent())
    return std::nullopt;
  return facts.key;
}

void PlayerComponent::cancelHealthyReplacementStop()
{
  if (intentControlThread()) {
    if (m_healthyStopLease) m_healthyStopLease->revoke();
    m_healthyStop.cancel();
  }
}

void PlayerComponent::failHealthyReplacementStop()
{
  if (m_healthyStopLease) m_healthyStopLease->revoke();
  m_healthyStop.fail();
}

bool PlayerComponent::observeHealthyReplacementStop(const mpv_event* event)
{
  if (!event || m_healthyStop.status() == Stop::Status::Unstarted) return false;
  if (!healthyStopCurrent()) { failHealthyReplacementStop(); return false; }
  if (event->event_id == MPV_EVENT_QUEUE_OVERFLOW || event->event_id == MPV_EVENT_SHUTDOWN ||
      event->event_id == MPV_EVENT_START_FILE || event->event_id == MPV_EVENT_HOOK ||
      event->event_id == MPV_EVENT_CLIENT_MESSAGE) {
    failHealthyReplacementStop(); return false;
  }
  const auto attempt = m_healthyStop.facts().key.attempt;
  if (event->event_id == MPV_EVENT_COMMAND_REPLY && event->reply_userdata == (stopTag | attempt)) {
    m_healthyStop.stopReply(event->error); reconcileHealthyStopFailure();
    if (m_healthyStop.status() == Stop::Status::Failed) m_healthyStopLease->revoke();
    return false;
  }
  if (event->event_id == MPV_EVENT_END_FILE) {
    if (!event->data) { failHealthyReplacementStop(); return false; }
    const auto* end = static_cast<const mpv_event_end_file*>(event->data);
    const bool matched = m_healthyStop.endFile(end->playlist_entry_id, end->reason == MPV_END_FILE_REASON_STOP);
    if (!matched) m_healthyStopLease->revoke();
    return matched;
  }
  if (event->event_id != MPV_EVENT_GET_PROPERTY_REPLY) return false;
  Stop::Query query;
  if (event->reply_userdata == (beforeIdleTag | attempt)) query = Stop::Query::BeforeIdle;
  else if (event->reply_userdata == (beforePositionTag | attempt)) query = Stop::Query::BeforePosition;
  else if (event->reply_userdata == (afterIdleTag | attempt)) query = Stop::Query::AfterIdle;
  else return false;
  const auto* property = static_cast<const mpv_event_property*>(event->data);
  const bool position = query == Stop::Query::BeforePosition;
  const bool valid = property && property->name && property->data &&
      !std::strcmp(property->name, position ? "playlist-pos" : "idle-active") &&
      property->format == (position ? MPV_FORMAT_INT64 : MPV_FORMAT_FLAG);
  const qint64 value = !valid ? 0 : position ? *static_cast<const int64_t*>(property->data)
                                           : *static_cast<const int*>(property->data);
  m_healthyStop.queryReply(query, event->error, valid, value);
  if (m_healthyStop.status() == Stop::Status::Failed) m_healthyStopLease->revoke();
  return false;
}
