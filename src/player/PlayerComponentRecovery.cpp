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
bool PlayerComponentRecovery::bind()
{
  if (bound_ || !player_->m_window) return bound_;
  item_ = player_->m_window->findChild<MpvVideoItem*>("video");
  if (!item_) return false;
  core_ = item_->recoveryCoreLease();
  if (!core_ || !core_->wakeRelay || !core_->bindExternalConsumer()) { core_.reset(); return false; }
  connect(core_->wakeRelay, &RendererRuntime::WakeRelay::nativeEventsAvailable, this,
          [this] { drain(); }, Qt::QueuedConnection);
  if (!core_->installExternalWakeup()) { disconnect(core_->wakeRelay, nullptr, this, nullptr); core_->unbindExternalConsumer(); core_.reset(); return false; }
  connect(item_, &MpvVideoItem::rendererUsabilityResult, this,
          [this](quint64 generation, quint64 request, bool usable) { usability(generation, request, usable); });
#ifdef MPVQT_RECOVERY_FRAME_API
  connect(item_, &MpvVideoItem::recoveryFrameRendered, this,
          [this](quint64 generation, const RecoveryFrameObservationV1& frame) { rendered(generation, frame); });
  connect(item_, &MpvVideoItem::recoveryFrameSubmitted, this,
          [this](quint64 generation, quint64 serial) { submitted(generation, serial); });
  connect(item_, &MpvVideoItem::recoveryFrameSwapped, this,
          [this](quint64 generation, quint64 serial) { swapped(generation, serial); });
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
  bound_ = false; core_.reset(); item_.clear();
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
void PlayerComponentRecovery::userChanged(bool clear)
{
  const auto previous = runtime_.activeKey();
  runtime_.userIntentChanged(); observation_.invalidate(); observationPending_ = awaitingUsability_ = false;
  frameGate_.invalidate(); recoveryLoad_.invalidate(); policy_.fail();
  frameGateActive_ = positionReady_ = recoveryLoaded_ = false;
  restoring_ = loadDispatched_ = resumePending_ = completed_ = false;
  recoveryRestarted_ = false; verifiedSource_ = 0;
  pauseId_ = unpauseId_ = 0;
  if (clear) { runtime_.invalidate(); return; }
  const auto intent = player_->m_playbackIntent.snapshot();
  if (intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) return;
  const AutomaticRecoverySession::Key current{previous.session, intent.revision, player_->m_mpvGeneration};
  if (runtime_.reviseActive(previous, current, intent.desired == PlaybackIntent::Desired::Paused))
    runtime_.updateTracks(current, player_->m_currentAudioStream, player_->m_currentSubtitleStream);
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
    if (native->event_id == MPV_EVENT_END_FILE && !internalEnd)
      player_->m_playbackIntent.invalidate(PlaybackIntent::Reason::UnattributedEvent);
    player_->handleMpvEvent(native);
  }
  player_->updatePlaybackState();
}
bool PlayerComponentRecovery::ownsEnd(const mpv_event* event) const
{
  if (!event || event->event_id != MPV_EVENT_END_FILE || !event->data || !sameIntent()) return false;
  const auto* end = static_cast<const mpv_event_end_file*>(event->data);
  return restoring_ &&
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
