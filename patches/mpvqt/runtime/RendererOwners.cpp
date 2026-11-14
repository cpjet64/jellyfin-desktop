#include "RendererOwners.h"
#include "mpvcontroller.h"
#include <QMetaObject>
#include <algorithm>
#include <vector>

namespace RendererRuntime {

static std::shared_ptr<GenerationOwner> &quarantined();

NativeLease::NativeLease(std::shared_ptr<CoreOwner> value) : owner(std::move(value)) {}
NativeLease::NativeLease(NativeLease &&other) noexcept : owner(std::move(other.owner)) {}
NativeLease &NativeLease::operator=(NativeLease &&other) noexcept
{
    if (this != &other) {
        if (owner) owner->leaveControl();
        owner = std::move(other.owner);
    }
    return *this;
}
NativeLease::~NativeLease() { if (owner) owner->leaveControl(); }
mpv_handle *NativeLease::get() const { return owner ? owner->handle : nullptr; }

static std::mutex &pendingMutex() { static auto *value = new std::mutex; return *value; }
static std::vector<std::shared_ptr<CoreOwner>> &pendingCores()
{
    static auto *value = new std::vector<std::shared_ptr<CoreOwner>>;
    return *value;
}

std::shared_ptr<CoreOwner> CoreOwner::adopt(mpv_handle *native, MpvController *control,
                                           QThread *thread, WakeRelay *relay)
{
    if (!native || !control || !thread || !relay || control->thread() != thread || relay->thread() != thread)
        return {};
    auto owner = std::make_shared<CoreOwner>();
    owner->handle = native;
    owner->controller = control;
    owner->worker = thread;
    owner->wakeRelay = relay;
    return owner;
}

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
    admittedControl.fetch_add(1);
    if (commandAuthority.load()) return true;
    leaveControl();
    return false;
}

void CoreOwner::leaveControl()
{
    admittedControl.fetch_sub(1);
    if (finalizationRequested.load(std::memory_order_acquire)) maybeFinalize();
}

NativeLease CoreOwner::acquireNative()
{
    if (!enterControl()) return {};
    return NativeLease(shared_from_this());
}

bool CoreOwner::bindExternalConsumer()
{
    std::lock_guard<std::mutex> lock(postingMutex);
    if (!commandAuthority.load() || externalConsumers.load() != 0) return false;
    externalConsumers.store(1);
    return true;
}

void CoreOwner::unbindExternalConsumer()
{
    {
        std::lock_guard<std::mutex> lock(postingMutex);
        externalConsumers.store(0);
    }
    maybeFinalize();
}

bool CoreOwner::postControl(std::function<void(mpv_handle *)> operation)
{
    std::unique_lock<std::mutex> lock(postingMutex);
    if (!operation || !controller || !enterControl()) return false;
    auto lease = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(controller, [lease, operation = std::move(operation)] {
        struct Leave { std::shared_ptr<CoreOwner> owner; ~Leave() { owner->leaveControl(); } } guard{lease};
        operation(lease->handle);
    }, Qt::QueuedConnection);
    lock.unlock();
    if (!queued) leaveControl();
    return queued;
}

void CoreOwner::revokeControl()
{
    std::lock_guard<std::mutex> lock(postingMutex);
    commandAuthority.store(false);
}

bool CoreOwner::installExternalWakeup()
{
    std::lock_guard<std::mutex> lock(wakeRegistrationMutex);
    if (wakeClosed.load(std::memory_order_acquire)) return false;
    if (!handle || !wakeRelay || !commandAuthority.load(std::memory_order_acquire)) return false;
    wakeAdmission.store(true, std::memory_order_release);
    mpv_set_wakeup_callback(handle, wakeCore, this);
    return true;
}

void CoreOwner::closeExternalWakeup()
{
    std::lock_guard<std::mutex> lock(wakeRegistrationMutex);
    if (wakeClosed.load(std::memory_order_acquire)) return;
    wakeAdmission.store(false, std::memory_order_release);
    if (handle) mpv_set_wakeup_callback(handle, nullptr, nullptr);
    wakeClosed.store(true, std::memory_order_release);
}

bool CoreOwner::requestFinalization()
{
    {
        std::lock_guard<std::mutex> admission(postingMutex);
        if (finalizationRequested.load()) return false;
        {
            std::lock_guard<std::mutex> retained(pendingMutex());
            pendingCores().push_back(shared_from_this());
        }
        commandAuthority.store(false);
        finalizationRequested.store(true, std::memory_order_release);
    }
    closeExternalWakeup();
    maybeFinalize();
    return true;
}

void CoreOwner::maybeFinalize()
{
    if (!finalizationRequested.load(std::memory_order_acquire) || commandAuthority.load()) return;
    if (!wakeClosed.load(std::memory_order_acquire) || admittedControl.load() != 0 ||
        liveGenerations.load(std::memory_order_acquire) != 0 || externalConsumers.load() != 0 ||
        wakeCallbacks.load(std::memory_order_acquire) != 0)
        return;
    bool expected = false;
    if (!finalizationQueued.compare_exchange_strong(expected, true)) return;
    if (!handle || !controller || !worker || !wakeRelay) return;
    auto owner = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(wakeRelay, [owner] {
        const bool posted = QMetaObject::invokeMethod(owner->controller, [owner] {
            if (owner->admittedControl.load() || owner->liveGenerations.load() || owner->externalConsumers.load()) {
                owner->finalizationQueued.store(false);
                owner->maybeFinalize();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(owner->wakeRegistrationMutex);
                mpv_terminate_destroy(owner->handle);
                owner->handle = nullptr;
            }
            owner->wakeRelay->deleteLater();
            owner->controller->deleteLater();
            QObject::connect(owner->worker, &QThread::finished, owner->worker, &QObject::deleteLater);
            QObject::connect(owner->worker, &QThread::finished, [owner] {
                std::lock_guard<std::mutex> lock(pendingMutex());
                auto &pending = pendingCores();
                pending.erase(std::remove(pending.begin(), pending.end(), owner), pending.end());
            });
            owner->worker->quit();
        }, Qt::QueuedConnection);
        if (!posted) owner->finalizationQueued.store(false);
    }, Qt::QueuedConnection);
    if (!queued) finalizationQueued.store(false, std::memory_order_release);
}

GenerationOwner::GenerationOwner(quint64 id, std::shared_ptr<CoreOwner> coreLease)
    : generation(id), core(std::move(coreLease))
{
    if (core) core->liveGenerations.fetch_add(1, std::memory_order_acq_rel);
}

std::shared_ptr<GenerationOwner> GenerationOwner::create(quint64 id, const std::shared_ptr<CoreOwner> &core)
{
    if (!id || !core) return {};
    std::lock_guard<std::mutex> lock(core->postingMutex);
    if (!core->commandAuthority.load() || core->finalizationRequested.load()) return {};
    return std::shared_ptr<GenerationOwner>(new GenerationOwner(id, core));
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

void GenerationOwner::notifyRenderUpdate()
{
    if (!observerAuthority.load(std::memory_order_acquire) || !core || !core->wakeRelay) return;
    auto owner = shared_from_this();
    QMetaObject::invokeMethod(core->wakeRelay, [owner] {
        if (owner->observerAuthority.load(std::memory_order_acquire))
            emit owner->core->wakeRelay->renderUpdateRequested(owner->generation);
    }, Qt::QueuedConnection);
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
    if (!context && !contextEverCreated) {
        retired.store(true, std::memory_order_release);
        core->liveGenerations.fetch_sub(1, std::memory_order_acq_rel);
        core->maybeFinalize();
        return true;
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
    if (core->finalizationRequested.load(std::memory_order_acquire)) core->maybeFinalize();
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
    owner->core->revokeControl();
    owner->core->closeExternalWakeup();
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
