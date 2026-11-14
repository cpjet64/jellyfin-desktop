#include "PlayerComponentRecovery.h"
#ifdef MPVQT_RECOVERY_RUNTIME
#include "AutomaticRecoveryNative.h"
#include "AutomaticRecoveryReload.h"
#include "MpvVideoItem.h"
#include "PlayerComponent.h"
#include "RendererOwners.h"
#include "system/WebClientFaultGuard.h"
#include <QCoreApplication>
#include <QQuickWindow>
#include <chrono>
#include <limits>

std::int64_t PlayerComponentRecovery::nowMs()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
PlayerComponentRecovery::PlayerComponentRecovery(PlayerComponent* player) : QObject(player), player_(player)
{
  timer_.setInterval(500);
  connect(&timer_, &QTimer::timeout, this, [this] { tick(); });
  connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] { userChanged(true); });
}
PlayerComponentRecovery::~PlayerComponentRecovery() { shutdown(); }
RendererRuntime::NativeLease PlayerComponentRecovery::acquireNative() const
{ return core_ ? core_->acquireNative() : RendererRuntime::NativeLease{}; }
bool PlayerComponentRecovery::bind()
{
  if (bound_ || !player_->m_window) return bound_;
  item_ = player_->m_window->findChild<MpvVideoItem*>("video");
  if (!item_) return false;
  core_ = item_->recoveryCoreLease();
  if (!core_ || !core_->wakeRelay || !core_->bindExternalConsumer()) { core_.reset(); return false; }
  lifetimeLease_ = core_->acquireNative();
  if (!lifetimeLease_.get()) { core_->unbindExternalConsumer(); core_.reset(); return false; }
  connect(core_->wakeRelay, &RendererRuntime::WakeRelay::nativeEventsAvailable, this,
          [this] { drain(); }, Qt::QueuedConnection);
  if (!core_->installExternalWakeup()) { disconnect(core_->wakeRelay, nullptr, this, nullptr); core_->unbindExternalConsumer(); lifetimeLease_ = {}; core_.reset(); return false; }
  connect(item_, &MpvVideoItem::rendererUsabilityResult, this,
          [this](quint64 generation, quint64 request, bool usable) { usability(generation, request, usable); });
#ifdef MPVQT_RECOVERY_FRAME_API
  connect(item_, &MpvVideoItem::recoveryFrameRendered, this,
          [this](quint64 generation, const RecoveryFrameObservationV1& frame) { rendered(generation, frame); });
  connect(item_, &MpvVideoItem::recoveryFrameSubmission, this,
          [this](quint64 generation, QtRecoverySubmissionV1 result) { submission(generation, result); });
#endif
#ifdef MPVQT_OWNED_SEEK_API
  connect(item_, &MpvVideoItem::recoveryFrameSubmissionOperation, this,
          [this](quint64 generation, QtRecoverySubmissionV1 value, quint64 operationId) {
            seekQtSubmission(generation, value, operationId);
          });
#endif
  bound_ = true; timer_.start(); return true;
}
void PlayerComponentRecovery::shutdown()
{
  timer_.stop(); userChanged(true);
  if (!core_) return;
  if (core_->wakeRelay) disconnect(core_->wakeRelay, nullptr, this, nullptr);
  core_->closeExternalWakeup();
  if (bound_) core_->unbindExternalConsumer();
  bound_ = false; lifetimeLease_ = {}; core_.reset(); item_.clear();
}
bool PlayerComponentRecovery::queue(const QStringList& command, const QString& url, const QVariantMap& options,
                                    const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle)
{
  if (!bound_ || !item_ || !core_ ||
      session_ == std::numeric_limits<std::uint64_t>::max()) return false;
  auto lease = core_->acquireNative();
  if (!lease.get()) return false;
  const auto intent = player_->m_playbackIntent.snapshot();
  const AutomaticRecoverySession::Key key{++session_, intent.revision, player_->m_mpvGeneration};
  return runtime_.submitUserLoad([&lease](std::uint64_t id, const char** args) {
    return mpv_command_async(lease.get(), id, args);
  }, key, command, url, options, metadata, audio, subtitle, !options.value("autoplay").toBool(), nowMs());
}
bool PlayerComponentRecovery::userChanged(bool clear)
{
  const bool cancelled = cancelOwnedSeek();
#ifdef MPVQT_OWNED_SEEK_API
  if (item_) item_->refreshRecoveryBootstrap();
#endif
  clearFrameAttempt();
  const auto previous = runtime_.activeKey();
  runtime_.userIntentChanged(); observation_.invalidate(); observationPending_ = awaitingUsability_ = false;
  readStall_.reset();
  if (restoring_ && recoveryLoad_.startedEntry() > 0) cancelledRecoveryEntry_ = recoveryLoad_.startedEntry();
  frameGate_.invalidate(); recoveryLoad_.invalidate(); policy_.fail();
  frameGateActive_ = positionReady_ = recoveryLoaded_ = false;
  restoring_ = loadDispatched_ = resumePending_ = completed_ = false;
  recoveryRestarted_ = false; verifiedSource_ = 0;
  recoveryTracksApplied_ = false;
  replacementTransition_ = false;
  pauseId_ = unpauseId_ = 0;
  deferredEntry_ = 0;
  if (clear) { seekPrepared_ = false; runtime_.invalidate(); return cancelled; }
  const auto intent = player_->m_playbackIntent.snapshot();
  if (intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) return cancelled;
  const AutomaticRecoverySession::Key current{previous.session, intent.revision, player_->m_mpvGeneration};
  if (runtime_.reviseActive(previous, current, intent.desired == PlaybackIntent::Desired::Paused))
    runtime_.updateTracks(current, player_->m_currentAudioStream, player_->m_currentSubtitleStream);
  return cancelled;
}
void PlayerComponentRecovery::drain()
{
  if (!bound_ || !core_) return;
  auto lease = core_->acquireNative();
  if (!lease.get()) return;
  for (;;) {
    mpv_event* native = mpv_wait_event(lease.get(), 0);
    if (native->event_id == MPV_EVENT_NONE) break;
    const bool internalEnd = ownsEnd(native);
    event(native, nowMs());
    player_->observeNativeReceipt(native);
    if (native->event_id == MPV_EVENT_END_FILE && !internalEnd) {
      cancelOwnedSeek(); player_->m_playbackIntent.invalidate(PlaybackIntent::Reason::UnattributedEvent); userChanged(true);
    }
    if (internalEnd) {
      const auto* end = static_cast<const mpv_event_end_file*>(native->data);
      if (!lastStartedEntry_ || lastStartedEntry_ == end->playlist_entry_id) {
        player_->m_inPlayback = false; player_->m_playbackCanceled = false; player_->m_playbackError.clear();
        replacementTransition_ = true; transitionDeadline_ = restoring_ ? policy_.deadlineMs() : nowMs() + 15000;
      }
    } else {
      if (native->event_id == MPV_EVENT_START_FILE) replacementTransition_ = false;
      player_->handleMpvEvent(native);
    }
  }
  if (!replacementTransition_) player_->updatePlaybackState();
}
bool PlayerComponentRecovery::ownsEnd(const mpv_event* event) const
{
  if (!event || event->event_id != MPV_EVENT_END_FILE || !event->data) return false;
  const auto* end = static_cast<const mpv_event_end_file*>(event->data);
  if (replacedEntry_ > 0 && end->playlist_entry_id == replacedEntry_ && end->reason == MPV_END_FILE_REASON_STOP) return true;
  return validRestore(nowMs()) &&
      end->playlist_entry_id == saved_.entry && end->reason == MPV_END_FILE_REASON_STOP;
}
void PlayerComponentRecovery::beginObservation(AutomaticRecoverySession::Key key, std::int64_t entry, std::int64_t now)
{
  if (!core_ || observationPending_) return;
  const auto dispatch = [this](std::uint64_t id, const char* name, mpv_format format) {
    auto lease = core_->acquireNative();
    return lease.get() ? mpv_get_property_async(lease.get(), id, name, format) : -1;
  };
  observationPending_ = observation_.begin(key, entry, runtime_.eventEpoch(), now, dispatch);
  observationStarted_ = now;
}
bool PlayerComponentRecovery::sameIntent() const
{
  const auto intent = player_->m_playbackIntent.snapshot();
  const auto active = runtime_.activeKey();
  return item_ && core_ && active.session == saved_.key.session && active.revision == saved_.key.revision &&
      intent.revision == saved_.key.revision &&
      player_->m_mpvGeneration == saved_.key.generation && intent.desired != PlaybackIntent::Desired::Stopped &&
      intent.desired != PlaybackIntent::Desired::Exiting && !WebClientFaultGuard::instance().rendererLost();
}
