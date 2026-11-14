#include "PlayerComponent.h"
#include "settings/SettingsComponent.h"
#include "settings/SettingsSection.h"
#include <MpvController>
#include <QDebug>
#include <QThread>
#include <QCoreApplication>
#include <QMetaObject>
#ifdef MPVQT_RECOVERY_RUNTIME
#include "PlayerComponentRecovery.h"
#include <atomic>
#include <memory>
#endif

#ifdef MPVQT_RECOVERY_RUNTIME
namespace {
// The lease admits only this already-received hook. It fences finalization,
// including after GUI authority has been revoked; it does not admit commands.
// The retained owner is the exact original core, so a later player binding
// cannot redirect this hook even when its numeric binding generation changes.
class PendingHook final : public std::enable_shared_from_this<PendingHook> {
public:
  using Operation = std::function<int(mpv_handle*)>;
  enum class Status { Reserved, Posted, Continued, PostFailed, NativeRejected };
  PendingHook(std::shared_ptr<RendererRuntime::CoreOwner> core, RendererRuntime::NativeLease lease, Operation operation)
      : core_(std::move(core)), lease_(std::move(lease)), operation_(std::move(operation)) {}
  ~PendingHook() { containUnresolved(); }
  void setQuitConnection(const QMetaObject::Connection& connection) { quitConnection_ = connection; }
  void dispatch() {
    if (dispatched_.exchange(true)) return;
    auto self = shared_from_this();
    QObject::disconnect(quitConnection_);
    status_.store(Status::Posted);
    if (!core_->controller || !QMetaObject::invokeMethod(core_->controller, [self] {
      const int result = self->operation_(self->lease_.get());
      if (result >= 0) { self->status_.store(Status::Continued); self->lease_ = {}; }
      else { self->status_.store(Status::NativeRejected); qWarning() << "Native hook continuation rejected:" << result; self->containUnresolved(); }
    }, Qt::QueuedConnection)) {
      status_.store(Status::PostFailed); qWarning() << "Native hook continuation could not be posted";
      containUnresolved();
    }
  }
private:
  void containUnresolved() {
    if (!lease_.get()) return;
    lease_.contain();
    lease_ = {};
  }
  std::shared_ptr<RendererRuntime::CoreOwner> core_;
  RendererRuntime::NativeLease lease_;
  Operation operation_;
  std::atomic<bool> dispatched_{false};
  std::atomic<Status> status_{Status::Reserved};
  QMetaObject::Connection quitConnection_;
};

std::shared_ptr<PendingHook> reserveHook(const std::shared_ptr<RendererRuntime::CoreOwner>& owner,
                                         PendingHook::Operation operation)
{
  if (!owner || !operation) return {};
  auto lease = owner->acquireNative();
  if (!lease.get()) return {};
  auto pending = std::make_shared<PendingHook>(owner, std::move(lease), std::move(operation));
  if (auto* app = QCoreApplication::instance())
    pending->setQuitConnection(QObject::connect(app, &QCoreApplication::aboutToQuit, app,
      [pending] { pending->dispatch(); }, Qt::DirectConnection));
  return pending;
}
}
#endif

#ifndef MPVQT_RECOVERY_RUNTIME
namespace {
void wakePlayer(void* context)
{
  emit static_cast<PlayerComponent*>(context)->onMpvEvents();
}
}
#endif

void PlayerComponent::setupMpvEvents()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  return;
#else
#ifdef MPVQT_EXTERNAL_EVENTS
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be initialized on the PlayerComponent thread");
  connect(this, &PlayerComponent::onMpvEvents, this, &PlayerComponent::handleMpvEvents,
          static_cast<Qt::ConnectionType>(Qt::QueuedConnection | Qt::UniqueConnection));
#endif
  mpv_set_wakeup_callback(m_mpv->mpv(), wakePlayer, this);
#ifdef MPVQT_EXTERNAL_EVENTS
  emit onMpvEvents();
#endif
#endif
}

void PlayerComponent::finishMpvEventSetup()
{
#ifdef MPVQT_RECOVERY_RUNTIME
  // The exclusive consumer performs its initial drain during binding.
#else
#ifndef MPVQT_EXTERNAL_EVENTS
  connect(this, &PlayerComponent::onMpvEvents, this, &PlayerComponent::handleMpvEvents, Qt::QueuedConnection);
#endif
  emit onMpvEvents();
#endif
}

void PlayerComponent::subscribeMpvSettings()
{
#ifdef MPVQT_EXTERNAL_EVENTS
  constexpr auto connection = Qt::UniqueConnection;
#else
  constexpr auto connection = Qt::AutoConnection;
#endif
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_AUDIO))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateAudioConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_VIDEO))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateVideoConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_SUBTITLES))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateSubtitleConfiguration, connection);
  if (auto* section = SettingsComponent::Get().getSection(SETTINGS_SECTION_OTHER))
    connect(section, &SettingsSection::valuesUpdated, this, &PlayerComponent::updateConfiguration, connection);
}

#ifdef MPVQT_EXTERNAL_EVENTS
bool PlayerComponent::tryBindMpvController(MpvController* controller)
{
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be bound on the PlayerComponent thread");
  if (!controller || m_mpv) return false;
  if (m_lastMpvController != controller) m_doAc3Transcoding = false;
  m_lastMpvController = controller;
  m_mpv = controller;
  ++m_mpvGeneration;
  return true;
}

void PlayerComponent::releaseMpvController(MpvController* controller)
{
  if (QThread::currentThread() != thread())
    qFatal("MpvQt event consumer must be released on the PlayerComponent thread");
  if (m_mpv != controller) return;
  cancelRecoverySeekBeforeIntent();
  invalidateNativeReceipt();
  m_playbackIntent.invalidate(PlaybackIntent::Reason::UnsupportedCommand);
  releaseRecovery();
  m_reloadAudioTimer.stop();
#ifndef MPVQT_RECOVERY_RUNTIME
  mpv_set_wakeup_callback(controller->mpv(), nullptr, nullptr);
#endif
  m_mpv = nullptr;
  ++m_mpvGeneration;
}
#endif

void PlayerComponent::scheduleMpvHook(const std::function<void()>& resume)
{
#ifdef MPVQT_C2_DIAGNOSTIC_ONLY
  if (m_c2HookDelayMs >= 0) {
    ++m_c2HooksScheduled;
    QTimer::singleShot(m_c2HookDelayMs, this, [this, resume] {
      resume();
      ++m_c2HooksCompleted;
    });
    return;
  }
#endif
  if (!switchDisplayFrameRate()) {
    resume();
    return;
  }
  const int pause = SettingsComponent::Get().value(SETTINGS_SECTION_VIDEO, "refreshrate.delay").toInt() * 1000;
  qInfo() << "waiting" << pause << "msec after rate switch before loading";
#ifdef MPVQT_RECOVERY_RUNTIME
  QTimer::singleShot(pause, QCoreApplication::instance(), resume);
#elif defined(MPVQT_EXTERNAL_EVENTS)
  QTimer::singleShot(pause, this, resume);
#else
  QTimer::singleShot(pause, resume);
#endif
}

void PlayerComponent::scheduleMpvHook(const QString& resumeId)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  const auto owner = m_recovery ? m_recovery->coreOwner() : nullptr;
  const auto id = resumeId.toUtf8();
  const auto pending = reserveHook(owner, [id](mpv_handle* handle) {
    const char* args[] = {"hook-ack", id.constData(), nullptr};
    return mpv_command(handle, args);
  });
  if (pending) scheduleMpvHook(std::function<void()>([pending] { pending->dispatch(); }));
#else
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto generation = m_mpvGeneration;
#endif
  scheduleMpvHook(std::function<void()>([=] {
#ifdef MPVQT_EXTERNAL_EVENTS
    if (!m_mpv || m_mpvGeneration != generation) return;
#endif
    qInfo() << "resuming loading";
    m_mpv->command(QStringList() << "hook-ack" << resumeId);
  }));
#endif
}

#if MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(1, 100)
void PlayerComponent::scheduleMpvHook(uint64_t id)
{
#ifdef MPVQT_RECOVERY_RUNTIME
  const auto owner = m_recovery ? m_recovery->coreOwner() : nullptr;
  const auto pending = reserveHook(owner, [id](mpv_handle* handle) { return mpv_hook_continue(handle, id); });
  if (pending) scheduleMpvHook(std::function<void()>([pending] { pending->dispatch(); }));
#else
#ifdef MPVQT_C2_DIAGNOSTIC_ONLY
  m_c2LastScheduledHookId = id;
#endif
#ifdef MPVQT_EXTERNAL_EVENTS
  const auto generation = m_mpvGeneration;
#endif
  scheduleMpvHook(std::function<void()>([=] {
#ifdef MPVQT_EXTERNAL_EVENTS
    if (!m_mpv || m_mpvGeneration != generation) return;
#endif
    qInfo() << "resuming loading";
#ifdef MPVQT_C2_DIAGNOSTIC_ONLY
    m_c2LastHookContinueResult = mpv_hook_continue(m_mpv->mpv(), id);
    m_c2LastContinuedHookId = id;
    ++m_c2HookContinueCalls;
#else
    mpv_hook_continue(m_mpv->mpv(), id);
#endif
  }));
#endif
}
#endif
