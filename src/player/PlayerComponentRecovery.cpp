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
#ifdef PLAYER_RECOVERY_ACTOR_TEST
void PlayerComponentRecovery::bindTestConsumer(RendererRuntime::WakeRelay* relay)
{
  if (!testEnabled_ || !relay || bound_) return;
  testRelay_ = relay;
  connect(relay, &RendererRuntime::WakeRelay::nativeEventsAvailable, this, [this] { drain(); }, Qt::QueuedConnection);
  bound_ = true;
}
#endif
std::int64_t PlayerComponentRecovery::clockNow() const
{
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (testClock_) return testClock_();
#endif
  return nowMs();
}
bool PlayerComponentRecovery::bind()
{
  if (bound_ || !player_->m_window) return bound_;
  item_ = player_->m_window->findChild<MpvVideoItem*>("video");
  if (!item_) return false;
  core_ = item_->recoveryCoreLease();
  if (!core_ || !core_->wakeRelay || !core_->wakeAdmission.load(std::memory_order_acquire) ||
      core_->wakeClosed.load(std::memory_order_acquire) || !core_->bindExternalConsumer()) { core_.reset(); return false; }
  lifetimeLease_ = core_->acquireNative();
  if (!lifetimeLease_.get() || !core_->wakeAdmission.load(std::memory_order_acquire) ||
      core_->wakeClosed.load(std::memory_order_acquire)) {
    core_->unbindExternalConsumer(); lifetimeLease_ = {}; core_.reset(); return false;
  }
  connect(core_->wakeRelay, &RendererRuntime::WakeRelay::nativeEventsAvailable, this,
          [this] { drain(); }, Qt::QueuedConnection);
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
  bound_ = true; timer_.start();
  QPointer<PlayerComponentRecovery> guard(this);
  drain();
  return guard && guard->bound_;
}
void PlayerComponentRecovery::shutdown()
{
  timer_.stop(); userChanged(true);
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (testEnabled_) {
    if (testRelay_) disconnect(testRelay_, nullptr, this, nullptr);
    testRelay_.clear(); bound_ = false; return;
  }
#endif
  if (!core_) return;
  if (core_->wakeRelay) disconnect(core_->wakeRelay, nullptr, this, nullptr);
  if (bound_) core_->unbindExternalConsumer();
  bound_ = false; lifetimeLease_ = {}; core_.reset(); item_.clear();
}
bool PlayerComponentRecovery::queue(const QStringList& command, const QString& url, const QVariantMap& options,
                                    const QVariantMap& metadata, const QVariant& audio, const QVariant& subtitle)
{
  if (!bound_ || (!player_->recoveryTestMode() && (!item_ || !core_)) ||
      session_ == std::numeric_limits<std::uint64_t>::max()) return false;
  const auto intent = player_->m_playbackIntent.snapshot();
  const AutomaticRecoverySession::Key key{++session_, intent.revision, player_->m_mpvGeneration};
  return beginOrdinaryLoad(command, url, options, metadata, audio, subtitle, key);
}
void PlayerComponentRecovery::beginUserReplacement()
{
  ordinaryReplacement_.revoke();
  replacementOldEntry_ = normalCurrentEntry_; replacementBeforeStart_ = startSerial_; replacementArming_ = true;
  replacementIdentity_ = {}; replacementDeadline_ = 0;
}
void PlayerComponentRecovery::replacementStopResult(bool admitted)
{
  if (!replacementArming_) return;
  replacementArming_ = false;
  const auto intent = player_->m_playbackIntent.snapshot();
  OrdinaryReplacementEnd::Identity current{intent.epoch, intent.request, player_->m_mpvGeneration, intent.revision};
  const auto now = clockNow();
  if (!admitted || current.epoch != replacementIdentity_.epoch || current.request != replacementIdentity_.request ||
      current.binding != replacementIdentity_.binding || current.revision != replacementIdentity_.revision ||
      now < 0 || now >= replacementDeadline_) { ordinaryReplacement_.revoke(); return; }
  ordinaryReplacement_.arm(replacementOldEntry_, replacementBeforeStart_, current, replacementDeadline_, true);
}
bool PlayerComponentRecovery::userChanged(bool clear, PlaybackIntent::ChangeKind kind)
{
  const auto intent = player_->m_playbackIntent.snapshot();
  const OrdinaryReplacementEnd::Identity identity{intent.epoch, intent.request, player_->m_mpvGeneration, intent.revision};
  const bool armReplacement = clear && replacementArming_;
  const auto now = clockNow();
  preferenceDeferred_ = !clear && preserveOrdinaryPreference(kind, intent);
  if (armReplacement) {
    ordinaryReplacement_.revoke(); replacementIdentity_ = identity;
    replacementDeadline_ = now >= 0 && now <= std::numeric_limits<std::int64_t>::max() - 15000 ? now + 15000 : 0;
  } else if ((intent.desired != PlaybackIntent::Desired::Playing && intent.desired != PlaybackIntent::Desired::Paused) ||
             !ordinaryReplacement_.preserve(kind, identity, now)) {
    replacementArming_ = false; ordinaryReplacement_.revoke();
  }
  if (!preferenceDeferred_) {
    if (ordinaryLoad_.pending) { if (clear) ordinaryLoad_ = {}; else flushOrdinaryLoad(false); }
    ordinarySelection_.revoke(); ordinaryAfterId_ = 0; ordinaryPreferences_ = {};
  }
  const bool cancelled = cancelOwnedSeek();
#ifdef MPVQT_OWNED_SEEK_API
  if (item_) item_->refreshRecoveryBootstrap();
#endif
  clearFrameAttempt();
  const auto previous = runtime_.activeKey();
  runtime_.userIntentChanged(); observation_.invalidate(); observationPending_ = awaitingUsability_ = false;
  if (preferenceDeferred_ && !ordinaryLoad_.pending) runtime_.invalidate();
  readStall_.reset();
  if (restoring_ && recoveryLoad_.startedEntry() > 0) cancelledRecoveryEntry_ = recoveryLoad_.startedEntry();
  frameGate_.invalidate(); recoveryLoad_.invalidate(); policy_.fail();
  frameGateActive_ = positionReady_ = recoveryLoaded_ = false;
  restoring_ = loadDispatched_ = resumePending_ = completed_ = false;
  recoveryRestarted_ = false; verifiedSource_ = 0;
  recoveryTracksApplied_ = false;
  if (!preferenceDeferred_) replacementTransition_ = false;
  pauseId_ = unpauseId_ = 0;
  if (!preferenceDeferred_) deferredEntry_ = 0;
  if (clear) { seekPrepared_ = false; runtime_.invalidate(); return cancelled; }
  if (intent.desired == PlaybackIntent::Desired::Stopped || intent.desired == PlaybackIntent::Desired::Exiting) return cancelled;
  const AutomaticRecoverySession::Key current{previous.session, intent.revision, player_->m_mpvGeneration};
  if (!preferenceDeferred_ && runtime_.reviseActive(previous, current, intent.desired == PlaybackIntent::Desired::Paused))
    runtime_.updateTracks(current, player_->m_currentAudioStream, player_->m_currentSubtitleStream);
  return cancelled;
}
void PlayerComponentRecovery::drain()
{
  if (!bound_ || !player_ || (!player_->recoveryTestMode() && (!core_ || !player_->m_mpv))) return;
  QPointer<PlayerComponentRecovery> guard(this);
  QPointer<PlayerComponent> playerGuard(player_);
  const auto owner = core_;
  const auto binding = player_->m_mpvGeneration;
  const auto current = [&] {
    return guard && playerGuard && guard->bound_ && guard->core_ == owner &&
        (playerGuard->recoveryTestMode() || playerGuard->m_mpv) && playerGuard->m_mpvGeneration == binding;
  };
  RendererRuntime::NativeLease lease;
  if (!player_->recoveryTestMode()) { lease = core_->acquireNative(); if (!lease.get()) return; }
#ifdef PLAYER_RECOVERY_ACTOR_TEST
  if (testEnabled_ && !nextNativeEvent_) return;
#endif
  for (;;) {
    mpv_event* native = nullptr;
#ifdef PLAYER_RECOVERY_ACTOR_TEST
    if (testEnabled_) native = nextNativeEvent_(); else
#endif
      native = mpv_wait_event(lease.get(), 0);
    if (!native) break;
    if (native->event_id == MPV_EVENT_NONE) break;
    const bool internalEnd = ownsEnd(native);
    event(native, clockNow());
    if (!current()) return;
    player_->observeNativeReceipt(native);
    if (!current()) return;
    if (native->event_id == MPV_EVENT_END_FILE && !internalEnd) {
      cancelOwnedSeek(); player_->m_playbackIntent.invalidate(PlaybackIntent::Reason::UnattributedEvent); userChanged(true);
    }
    if (internalEnd) {
      const auto* end = static_cast<const mpv_event_end_file*>(native->data);
      if (!lastStartedEntry_ || lastStartedEntry_ == end->playlist_entry_id) {
        player_->m_inPlayback = false; player_->m_playbackCanceled = false; player_->m_playbackError.clear();
        replacementTransition_ = true; transitionDeadline_ = restoring_ ? policy_.deadlineMs() : clockNow() + 15000;
      }
    } else {
      if (native->event_id == MPV_EVENT_START_FILE) replacementTransition_ = false;
      player_->handleMpvEvent(native);
    }
    if (!current()) return;
  }
  if (!replacementTransition_) player_->updatePlaybackState();
}
bool PlayerComponentRecovery::ownsEnd(const mpv_event* event)
{
  if (!event || event->event_id != MPV_EVENT_END_FILE || !event->data) return false;
  const auto* end = static_cast<const mpv_event_end_file*>(event->data);
  const auto intent = player_->m_playbackIntent.snapshot();
  const OrdinaryReplacementEnd::Identity identity{intent.epoch, intent.request, player_->m_mpvGeneration, intent.revision};
  if (ordinaryReplacement_.consume(end->playlist_entry_id, end->reason == MPV_END_FILE_REASON_STOP, startSerial_,
                                   identity, clockNow())) return true;
  return validRestore(clockNow()) &&
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
