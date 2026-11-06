#include "player/PlayerComponent.h"
#include <MpvController>
#include <QThread>
#include <utility>
#include <stdexcept>

using HealthyRetirement::Closure;
using HealthyRetirement::WakeClosure;

bool PlayerComponent::healthyPrepareWake(std::uint64_t generation,
                                         const HealthyModuleIdentity::VerifiedModule& module,
                                         QObject* dispatcher)
{
  if (QThread::currentThread() != thread() || m_mpv || m_healthyWake || !generation ||
      !dispatcher || dispatcher->thread() != thread() ||
      !module.stillValid(generation) || module.wakeSourceContract(generation) !=
      HealthyModuleIdentity::SourceContract::BuilderSourceQualified) return false;
  auto weak = std::make_shared<std::weak_ptr<WakeClosure>>();
  const QPointer<PlayerComponent> player{this};
  auto wake = std::make_shared<WakeClosure>(generation, [dispatcher, player, weak](std::uint64_t gen) {
    if (!QMetaObject::invokeMethod(dispatcher, [player, weak, gen] {
      auto state = weak->lock();
      if (player && state && state->deliveryAllowed(gen)) emit player->onMpvEvents();
    }, Qt::QueuedConnection)) throw std::runtime_error("wake delivery was not queued");
  });
  *weak = wake;
  m_healthyWake = std::move(wake);
  m_healthyWakeModule = &module;
  return true;
}

Closure::EventResult PlayerComponent::healthyCloseEvents()
{
  if (QThread::currentThread() != thread() || !m_mpv || !m_mpv->mpv() ||
      !m_healthyWake || !m_healthyWakeModule || m_healthyEventsClosed || m_healthyEventActive)
    return Closure::EventResult::Failed;
  if (!m_healthyEventFencing) {
    handleMpvEvents();
    if (m_healthyHooks) return Closure::EventResult::Pending;
    m_healthyEventFencing = true;
    m_healthyWake->closeAdmission();
    auto* handle = m_mpv->mpv();
    auto task = std::make_shared<HealthyRetirement::WakeUnregisterTask>();
    task->wake = m_healthyWake;
    task->module = m_healthyWakeModule;
    m_healthyWakeTask = task;
    m_healthyWakeThread = QThread::create([task, handle] {
      task->succeeded.store(task->wake->unregisterOnce(
          [handle] { mpv_set_wakeup_callback(handle, nullptr, nullptr); }, *task->module));
      task->returned.store(true);
    });
    m_healthyWakeThread->start();
    return Closure::EventResult::Pending;
  }
  if (!m_healthyWakeTask->returned.load() || !m_healthyWakeThread->wait(0)) return Closure::EventResult::Pending;
  delete m_healthyWakeThread;
  m_healthyWakeThread = nullptr;
  if (!m_healthyWakeTask->succeeded.load() || !m_healthyWake->drained()) return Closure::EventResult::Failed;
  m_healthyEventFencing = false;
  handleMpvEvents();
  if (m_healthyHooks || m_healthyEventActive) return Closure::EventResult::Failed;
  m_healthyEventsClosed = true;
  m_mpv = nullptr;
  return Closure::EventResult::Closed;
}
