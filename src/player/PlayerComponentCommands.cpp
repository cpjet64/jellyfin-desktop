#include "PlayerComponent.h"
#include "system/WebClientFaultGuard.h"
#include <MpvController>
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
#include "PlayerIntentObservation.h"
#endif

bool PlayerComponent::load(const QString& url, const QVariantMap& options, const QVariantMap& metadata,
                           const QVariant& audioStream, const QVariant& subtitleStream)
{
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
  PlayerIntentObservation::Scope observed(PlayerIntentObservation::Kind::LoadEnter);
#endif
  stop();
  queueMedia(url, options, metadata, audioStream, subtitleStream);
  return true;
}

void PlayerComponent::stop()
{
#ifdef PLAYER_INTENT_DIAGNOSTIC_ONLY
  using Observation = PlayerIntentObservation;
  Observation::Scope observed(Observation::Kind::StopEnter);
  if (WebClientFaultGuard::instance().rendererLost()) {
    Observation::stopResult(Observation::Outcome::GuardRejected);
    return;
  }
  const auto result = stopMpv();
  const auto outcome = result.status == NativeStopResult::Status::Unavailable ? Observation::Outcome::Unavailable :
    result.status == NativeStopResult::Status::Error ? Observation::Outcome::Error : Observation::Outcome::Accepted;
  Observation::stopResult(outcome, result.error);
#else
  if (!WebClientFaultGuard::instance().rendererLost()) stopMpv();
#endif
}

PlayerComponent::NativeStopResult PlayerComponent::requestNativeStop()
{
  return stopMpv();
}

PlayerComponent::NativeStopResult PlayerComponent::stopMpv()
{
  if (!m_mpv || !m_mpv->mpv()) return {NativeStopResult::Status::Unavailable, 0};
  return checkedStop(m_mpv->mpv(), mpv_command);
}

PlayerComponent::NativeStopResult PlayerComponent::checkedStop(mpv_handle* handle, int (*command)(mpv_handle*, const char**))
{
  const char* args[] = {"stop", nullptr};
  const int error = command(handle, args);
  return {error < 0 ? NativeStopResult::Status::Error : NativeStopResult::Status::Accepted, error};
}
