#include "player/PlayerComponent.h"
#include <MpvController>
#include <QThread>
#include <utility>
#include <stdexcept>

using HealthyRetirement::Closure;
using HealthyRetirement::WakeClosure;

bool PlayerComponent::healthyPrepareWake(std::uint64_t generation,
                                         const HealthyModuleIdentity::VerifiedModule& module)
{
  if (QThread::currentThread() != thread() || m_mpv || m_healthyWake || !generation ||
      !module.stillValid(generation) || module.wakeSourceContract(generation) !=
      HealthyModuleIdentity::SourceContract::BuilderSourceQualified) return false;
  auto weak = std::make_shared<std::weak_ptr<WakeClosure>>();
  auto wake = std::make_shared<WakeClosure>(generation, [this, weak](std::uint64_t gen) {
    if (!QMetaObject::invokeMethod(this, [this, weak, gen] {
      auto state = weak->lock();
      if (state && state->deliveryAllowed(gen)) emit onMpvEvents();
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
    m_healthyWakeThread = QThread::create([this, handle] {
      m_healthyWakeSucceeded.store(m_healthyWake->unregisterOnce(
          [handle] { mpv_set_wakeup_callback(handle, nullptr, nullptr); }, *m_healthyWakeModule));
      m_healthyWakeReturned.store(true);
    });
    m_healthyWakeThread->start();
    return Closure::EventResult::Pending;
  }
  if (!m_healthyWakeReturned.load() || !m_healthyWakeThread->wait(0)) return Closure::EventResult::Pending;
  delete m_healthyWakeThread;
  m_healthyWakeThread = nullptr;
  if (!m_healthyWakeSucceeded.load() || !m_healthyWake->drained()) return Closure::EventResult::Failed;
  m_healthyEventFencing = false;
  handleMpvEvents();
  if (m_healthyHooks || m_healthyEventActive) return Closure::EventResult::Failed;
  m_healthyEventsClosed = true;
  m_mpv = nullptr;
  return Closure::EventResult::Closed;
}
