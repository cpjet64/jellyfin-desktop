#include "RendererOwners.h"
#include "mpvcontroller.h"
#include <QMetaObject>

namespace RendererRuntime {

static std::shared_ptr<GenerationOwner> &quarantined();

static void wakeCore(void *opaque)
{
    auto *owner = static_cast<CoreOwner *>(opaque);
    owner->wakeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lock(owner->postingMutex);
        if (owner->wakeAdmission.load(std::memory_order_acquire) && owner->wakeRelay) {
            auto lease = owner->shared_from_this();
            QMetaObject::invokeMethod(owner->wakeRelay, [lease] {
                if (lease->wakeAdmission.load(std::memory_order_acquire)) lease->wakeRelay->notify();
            }, Qt::QueuedConnection);
        }
    }
    owner->wakeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
}

bool CoreOwner::enterControl()
{
    admittedControl.fetch_add(1, std::memory_order_acq_rel);
    if (commandAuthority.load(std::memory_order_acquire)) return true;
    admittedControl.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}

void CoreOwner::leaveControl()
{
    admittedControl.fetch_sub(1, std::memory_order_acq_rel);
}

bool CoreOwner::postControl(std::function<void(mpv_handle *)> operation)
{
    std::lock_guard<std::mutex> lock(postingMutex);
    if (!operation || !controller || !enterControl()) return false;
    auto lease = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(controller, [lease, operation = std::move(operation)] {
        struct Leave { std::shared_ptr<CoreOwner> owner; ~Leave() { owner->leaveControl(); } } guard{lease};
        operation(lease->handle);
    }, Qt::QueuedConnection);
    if (!queued) leaveControl();
    return queued;
}

void CoreOwner::revokeControl()
{
    std::lock_guard<std::mutex> lock(postingMutex);
    commandAuthority.store(false, std::memory_order_release);
}

bool CoreOwner::installExternalWakeup()
{
    if (!handle || !wakeRelay || !commandAuthority.load(std::memory_order_acquire)) return false;
    wakeAdmission.store(true, std::memory_order_release);
    mpv_set_wakeup_callback(handle, wakeCore, this);
    return true;
}

void CoreOwner::closeExternalWakeup()
{
    wakeAdmission.store(false, std::memory_order_release);
    if (handle) mpv_set_wakeup_callback(handle, nullptr, nullptr);
}

bool CoreOwner::requestFinalization()
{
    revokeControl();
    closeExternalWakeup();
    if (!handle || !controller || !worker || admittedControl.load(std::memory_order_acquire) != 0 ||
        liveGenerations.load(std::memory_order_acquire) != 0 || wakeCallbacks.load(std::memory_order_acquire) != 0)
        return false;
    bool expected = false;
    if (!finalizationRequested.compare_exchange_strong(expected, true)) return false;
    auto owner = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(controller, [owner] {
        mpv_set_wakeup_callback(owner->handle, nullptr, nullptr);
        mpv_terminate_destroy(owner->handle);
        owner->handle = nullptr;
        owner->worker->quit();
    }, Qt::QueuedConnection);
    if (!queued) finalizationRequested.store(false, std::memory_order_release);
    return queued;
}

GenerationOwner::GenerationOwner(quint64 id, std::shared_ptr<CoreOwner> coreLease)
    : generation(id), core(std::move(coreLease))
{
    if (core) core->liveGenerations.fetch_add(1, std::memory_order_acq_rel);
}

bool GenerationOwner::enterCallback()
{
    callbacks.fetch_add(1, std::memory_order_acq_rel);
    if (callbackAdmission.load(std::memory_order_acquire)) return true;
    callbacks.fetch_sub(1, std::memory_order_acq_rel);
    return false;
}

void GenerationOwner::leaveCallback()
{
    callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

void GenerationOwner::revoke()
{
    observerAuthority.store(false, std::memory_order_release);
    callbackAdmission.store(false, std::memory_order_release);
}

bool GenerationOwner::retireOnOriginalContext()
{
    revoke();
    if (retired.load(std::memory_order_acquire)) return true;
    if (context) mpv_render_context_set_update_callback(context, nullptr, nullptr);
    if (callbacks.load(std::memory_order_acquire) != 0) {
        blocked.store(true, std::memory_order_release);
        return false;
    }
    if (!context || !originalThread || !originalContext ||
        QThread::currentThread() != originalThread ||
        QOpenGLContext::currentContext() != originalContext || !originalContext->isValid()) {
        blocked.store(true, std::memory_order_release);
        return false;
    }
    mpv_render_context_free(context);
    context = nullptr;
    retired.store(true, std::memory_order_release);
    core->liveGenerations.fetch_sub(1, std::memory_order_acq_rel);
    return true;
}

bool RetirementCoordinator::retain(const std::shared_ptr<GenerationOwner> &owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner || !owner->core || !owner->generation || retained || quarantined()) return false;
    retained = owner;
    return true;
}

bool RetirementCoordinator::releaseRetired(const std::shared_ptr<GenerationOwner> &owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (retained != owner || !owner || !owner->retired.load(std::memory_order_acquire)) return false;
    retained.reset();
    return true;
}

static std::shared_ptr<GenerationOwner> &quarantined()
{
    static auto *slot = new std::shared_ptr<GenerationOwner>();
    return *slot;
}

bool RetirementCoordinator::quarantine(const std::shared_ptr<GenerationOwner> &owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner || retained != owner || quarantined()) return false;
    owner->revoke();
    owner->core->commandAuthority.store(false, std::memory_order_release);
    owner->blocked.store(true, std::memory_order_release);
    quarantined() = owner;
    retained.reset();
    return true;
}

bool RetirementCoordinator::hasQuarantine() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return static_cast<bool>(quarantined());
}

RetirementCoordinator &coordinator()
{
    static auto *instance = new RetirementCoordinator();
    return *instance;
}
}
