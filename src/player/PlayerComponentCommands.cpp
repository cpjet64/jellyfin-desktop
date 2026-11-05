#include "PlayerComponent.h"
#include "system/WebClientFaultGuard.h"
#include <MpvController>
#include <QRandomGenerator>
#include <QScopedValueRollback>
#include <QThread>
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
#include "PlayerIntentObservation.h"
#endif

bool PlayerComponent::load(const QString& url, const QVariantMap& options, const QVariantMap& metadata,
                           const QVariant& audioStream, const QVariant& subtitleStream)
{
  if (!intentControlThread()) return false;
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
  PlayerIntentObservation::Scope observed(PlayerIntentObservation::Kind::LoadEnter);
#endif
  m_playbackIntent.play(!options.value("autoplay").toBool());
  if (options.contains("startMilliseconds")) {
    bool valid = false;
    const auto position = options.value("startMilliseconds").toLongLong(&valid);
    m_playbackIntent.seek(valid ? position : -1);
  }
  QScopedValueRollback<bool> replacement(m_replacementLoad, true);
  {
    QScopedValueRollback<bool> cleanup(m_replacementStop, true);
    stop();
  }
  queueMedia(url, options, metadata, audioStream, subtitleStream);
  return true;
}

void PlayerComponent::stop()
{
  if (!intentControlThread()) return;
  if (!m_replacementStop) m_playbackIntent.stop(PlaybackIntent::Reason::WebStopUnclassified);
  const auto revision = m_playbackIntent.snapshot().revision;
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
  using Observation = PlayerIntentObservation;
  Observation::Scope observed(Observation::Kind::StopEnter);
  if (WebClientFaultGuard::instance().rendererLost()) {
    if (!m_replacementStop) m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::GuardRejected);
    Observation::stopResult(Observation::Outcome::GuardRejected);
    return;
  }
  const auto result = stopMpv();
  if (!m_replacementStop) m_playbackIntent.dispatch(revision,
    result.status == NativeStopResult::Status::Unavailable ? PlaybackIntent::Dispatch::Unavailable :
    result.status == NativeStopResult::Status::Error ? PlaybackIntent::Dispatch::NativeRejected : PlaybackIntent::Dispatch::NativeAccepted);
  const auto outcome = result.status == NativeStopResult::Status::Unavailable ? Observation::Outcome::Unavailable :
    result.status == NativeStopResult::Status::Error ? Observation::Outcome::Error : Observation::Outcome::Accepted;
  Observation::stopResult(outcome, result.error);
#else
  if (WebClientFaultGuard::instance().rendererLost()) {
    if (!m_replacementStop) m_playbackIntent.dispatch(revision, PlaybackIntent::Dispatch::GuardRejected);
    return;
  }
  const auto result = stopMpv();
  if (!m_replacementStop) m_playbackIntent.dispatch(revision,
    result.status == NativeStopResult::Status::Unavailable ? PlaybackIntent::Dispatch::Unavailable :
    result.status == NativeStopResult::Status::Error ? PlaybackIntent::Dispatch::NativeRejected : PlaybackIntent::Dispatch::NativeAccepted);
#endif
}

PlayerComponent::NativeStopResult PlayerComponent::requestNativeStop()
{
  if (!intentControlThread()) return {NativeStopResult::Status::Unavailable, 0};
  m_playbackIntent.stop(PlaybackIntent::Reason::NativeStop);
  const auto revision = m_playbackIntent.snapshot().revision;
  const auto result = stopMpv();
  m_playbackIntent.dispatch(revision, result.status == NativeStopResult::Status::Unavailable ? PlaybackIntent::Dispatch::Unavailable :
    result.status == NativeStopResult::Status::Error ? PlaybackIntent::Dispatch::NativeRejected : PlaybackIntent::Dispatch::NativeAccepted);
  return result;
}

bool PlayerComponent::intentControlThread() const
{
  return QThread::currentThread() == thread();
}

quint64 PlayerComponent::newIntentEpoch()
{
  static const quint64 value = [] {
    const auto random = QRandomGenerator::system()->generate64() & PlaybackIntent::MaxSafeInteger;
    return random ? random : 1;
  }();
  return value;
}

PlayerComponent::NativeStopResult PlayerComponent::stopMpv()
{
  if (m_stopDispatch) return m_stopDispatch();
  if (!m_mpv || !m_mpv->mpv()) return {NativeStopResult::Status::Unavailable, 0};
  return checkedStop(m_mpv->mpv(), mpv_command);
}

PlayerComponent::NativeStopResult PlayerComponent::checkedStop(mpv_handle* handle, int (*command)(mpv_handle*, const char**))
{
  const char* args[] = {"stop", nullptr};
  const int error = command(handle, args);
  return {error < 0 ? NativeStopResult::Status::Error : NativeStopResult::Status::Accepted, error};
}
