#include "RendererOwners.h"
#include "mpvcontroller.h"
#include <QMetaObject>
#include <algorithm>
#include <vector>

namespace RendererRuntime {

struct FaultSlot {
    std::mutex mutex;
    quint64 next = 1;
    quint64 reservation = 0;
    quint64 waiter = 0;
    quint64 dispatched = 0;
    quint64 revision = 0;
    QObject *dispatcher = nullptr;
    AdmissionContinuation continuation;
    std::shared_ptr<CoreOwner> active;
    QThread *joinHelper = nullptr;
    QObject *lifecycleDispatcher = nullptr;
    std::shared_ptr<CoreOwner> core;
    std::shared_ptr<GenerationOwner> generation;
};
static FaultSlot &faultSlot() { static auto *slot = new FaultSlot; return *slot; }

static void deliverWaiterLocked(FaultSlot &slot, AdmissionStatus status)
{
    if (!slot.waiter || !slot.dispatcher || !slot.continuation) return;
    auto callback = std::move(slot.continuation);
    const auto token = slot.waiter;
    const auto revision = slot.revision;
    slot.waiter = 0;
    slot.dispatched = token;
    if (status == AdmissionStatus::Reserved) slot.reservation = token;
    const bool posted = QMetaObject::invokeMethod(slot.dispatcher, [callback, status, token, revision] {
        callback(status, token, revision);
    }, Qt::QueuedConnection);
    if (!posted) {
        if (slot.reservation == token) slot.reservation = 0;
        slot.dispatched = 0;
    }
}

AdmissionResult reserveCore(QObject *dispatcher, quint64 revision, AdmissionContinuation callback)
{
    if (!dispatcher || !callback || dispatcher->thread() != QThread::currentThread())
        return {AdmissionStatus::Busy, 0};
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.core) return {AdmissionStatus::Quarantined, 0};
    if (slot.waiter) return {AdmissionStatus::Busy, 0};
    if (!slot.next) return {AdmissionStatus::Busy, 0};
    const auto token = slot.next;
    ++slot.next; // Unsigned wrap leaves zero as the permanent exhausted state.
    if (!slot.active && !slot.reservation) {
        slot.reservation = token;
        return {AdmissionStatus::Reserved, token};
    }
    slot.waiter = token;
    slot.revision = revision;
    slot.dispatcher = dispatcher;
    slot.continuation = std::move(callback);
    return {AdmissionStatus::Deferred, token};
}

void cancelCoreReservation(quint64 token)
{
    if (!token) return;
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.waiter == token) { slot.waiter = 0; slot.continuation = {}; slot.dispatcher = nullptr; }
    if (slot.dispatched == token) slot.dispatched = 0;
    if (slot.reservation == token) { slot.reservation = 0; deliverWaiterLocked(slot, AdmissionStatus::Reserved); }
}

void closeAdmissionDispatcher(QObject *dispatcher)
{
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.dispatcher == dispatcher) {
        slot.waiter = 0;
        slot.continuation = {};
        slot.dispatcher = nullptr;
        if (slot.reservation == slot.dispatched) slot.reservation = 0;
        slot.dispatched = 0;
    }
    if (slot.lifecycleDispatcher == dispatcher) slot.lifecycleDispatcher = nullptr;
}

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
void NativeLease::contain()
{
    if (owner) owner->blockUnresolvedHook();
}

static std::mutex &pendingMutex() { static auto *value = new std::mutex; return *value; }
static std::vector<std::shared_ptr<CoreOwner>> &pendingCores()
{
    static auto *value = new std::vector<std::shared_ptr<CoreOwner>>;
    return *value;
}

static void startConfirmedJoin(const std::shared_ptr<CoreOwner> &owner, QObject *dispatcher)
{
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.lifecycleDispatcher != dispatcher || slot.active != owner || slot.core || slot.joinHelper ||
        !owner->nativeDestroyed.load()) return;
    auto joined = std::make_shared<std::atomic<bool>>(false);
    QThread *helper = QThread::create([owner, joined] {
        joined->store(owner->worker->wait(15000), std::memory_order_release);
    });
    slot.joinHelper = helper;
    QObject::connect(helper, &QThread::finished, dispatcher, [owner, helper, joined] {
        const bool complete = joined->load(std::memory_order_acquire);
        {
            auto &state = faultSlot();
            std::lock_guard<std::mutex> guard(state.mutex);
            if (state.joinHelper == helper) state.joinHelper = nullptr;
            if (complete && !state.core && state.active == owner) {
                state.active.reset();
                deliverWaiterLocked(state, AdmissionStatus::Reserved);
            } else if (!complete && state.active == owner) {
                deliverWaiterLocked(state, AdmissionStatus::Busy);
            }
        }
        if (complete && !owner->faultQuarantined.load()) {
            owner->worker->deleteLater();
            std::lock_guard<std::mutex> guard(pendingMutex());
            auto &pending = pendingCores();
            pending.erase(std::remove(pending.begin(), pending.end(), owner), pending.end());
        }
        helper->deleteLater();
    }, Qt::QueuedConnection);
    helper->start();
}

std::shared_ptr<CoreOwner> CoreOwner::adoptReserved(quint64 token, mpv_handle *native, MpvController *control,
                                                   QThread *thread, WakeRelay *relay, QObject *lifecycleDispatcher)
{
    if (!native) return {};
    return adoptInternal(token, native, control, thread, relay, lifecycleDispatcher, false);
}

std::shared_ptr<CoreOwner> CoreOwner::adoptFailedReservation(quint64 token, MpvController *control,
    QThread *thread, WakeRelay *relay, QObject *lifecycleDispatcher)
{
    return adoptInternal(token, nullptr, control, thread, relay, lifecycleDispatcher, true);
}

std::shared_ptr<CoreOwner> CoreOwner::adoptInternal(quint64 token, mpv_handle *native, MpvController *control,
    QThread *thread, WakeRelay *relay, QObject *lifecycleDispatcher, bool failed)
{
    if (!control || !thread || !relay || !lifecycleDispatcher ||
        lifecycleDispatcher->thread() != QThread::currentThread() ||
        control->thread() != thread || relay->thread() != thread)
        return {};
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    if (slot.core || slot.active || !token || slot.reservation != token) return {};
    auto owner = std::shared_ptr<CoreOwner>(new CoreOwner());
    owner->handle = native;
    owner->controller = control;
    owner->worker = thread;
    owner->wakeRelay = relay;
    owner->failedInitialization = failed;
    if (failed) owner->commandAuthority.store(false);
    slot.active = owner;
    slot.lifecycleDispatcher = lifecycleDispatcher;
    QObject::connect(thread, &QThread::finished, lifecycleDispatcher, [owner, lifecycleDispatcher] {
        startConfirmedJoin(owner, lifecycleDispatcher);
    }, Qt::QueuedConnection);
    slot.reservation = 0;
    slot.dispatched = 0;
    return owner;
}

void CoreOwner::blockUnresolvedHook()
{
    auto owner = shared_from_this();
    auto &slot = faultSlot();
    {
        std::lock_guard<std::mutex> lock(slot.mutex);
        faultQuarantined.store(true, std::memory_order_release);
        if (!slot.core) slot.core = owner;
        deliverWaiterLocked(slot, AdmissionStatus::Quarantined);
    }
    requestQuarantineStop();
    revokeControl();
    closeExternalWakeup();
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
    if (commandAuthority.load() && !faultQuarantined.load(std::memory_order_acquire)) return true;
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
    if (!commandAuthority.load() || faultQuarantined.load() || externalConsumers.load() != 0) return false;
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

void CoreOwner::requestQuarantineStop()
{
    bool expected = false;
    if (!quarantineStopStarted.compare_exchange_strong(expected, true)) return;
    auto owner = shared_from_this();
    std::unique_lock<std::mutex> lock(postingMutex);
    const bool admitted = controller && commandAuthority.load();
    if (admitted) admittedControl.fetch_add(1);
    bool queued = false;
    if (admitted) {
        queued = QMetaObject::invokeMethod(controller, [owner] {
            struct Leave { std::shared_ptr<CoreOwner> owner; ~Leave() { owner->leaveControl(); } } guard{owner};
            const char *args[] = {"stop", nullptr};
            const int result = mpv_command_async(owner->handle, quarantineStopUserdata, args);
            owner->quarantineStopNativeResult.store(result, std::memory_order_release);
        }, Qt::QueuedConnection);
    }
    commandAuthority.store(false);
    lock.unlock();
    if (admitted && !queued) leaveControl();
    quarantineStopQueue.store(queued ? QuarantineStopQueue::Queued : QuarantineStopQueue::Rejected,
                              std::memory_order_release);
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
    if (!handle || !wakeRelay || !commandAuthority.load(std::memory_order_acquire) ||
        faultQuarantined.load(std::memory_order_acquire)) return false;
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
    if (!finalizationRequested.load(std::memory_order_acquire) || commandAuthority.load() ||
        faultQuarantined.load(std::memory_order_acquire)) return;
    if (!wakeClosed.load(std::memory_order_acquire) || admittedControl.load() != 0 ||
        liveGenerations.load(std::memory_order_acquire) != 0 || externalConsumers.load() != 0 ||
        wakeCallbacks.load(std::memory_order_acquire) != 0)
        return;
    bool expected = false;
    if (!finalizationQueued.compare_exchange_strong(expected, true)) return;
    if ((!handle && !failedInitialization) || !controller || !worker || !wakeRelay) return;
    auto owner = shared_from_this();
    const bool queued = QMetaObject::invokeMethod(wakeRelay, [owner] {
        const bool posted = QMetaObject::invokeMethod(owner->controller, [owner] {
            if (owner->faultQuarantined.load() || owner->admittedControl.load() ||
                owner->liveGenerations.load() || owner->externalConsumers.load()) {
                owner->finalizationQueued.store(false);
                owner->maybeFinalize();
                return;
            }
            {
                std::lock_guard<std::mutex> lock(owner->wakeRegistrationMutex);
                if (owner->handle) mpv_terminate_destroy(owner->handle);
                owner->handle = nullptr;
                owner->nativeDestroyed.store(true, std::memory_order_release);
            }
            owner->wakeRelay->deleteLater();
            owner->controller->deleteLater();
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
    if (!core->commandAuthority.load() || core->finalizationRequested.load() || core->faultQuarantined.load()) return {};
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
    if (!owner || !owner->core || !owner->generation || retained || hasQuarantine()) return false;
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

bool RetirementCoordinator::quarantine(const std::shared_ptr<GenerationOwner> &owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner || retained != owner) return false;
    auto &slot = faultSlot();
    {
        std::lock_guard<std::mutex> faultLock(slot.mutex);
        if ((slot.core && slot.core != owner->core) || slot.active != owner->core) return false;
        owner->core->faultQuarantined.store(true, std::memory_order_release);
        slot.core = owner->core;
        slot.generation = owner;
        deliverWaiterLocked(slot, AdmissionStatus::Quarantined);
    }
    owner->revoke();
    owner->core->requestQuarantineStop();
    owner->core->revokeControl();
    owner->core->closeExternalWakeup();
    owner->blocked.store(true, std::memory_order_release);
    retained.reset();
    return true;
}

bool RetirementCoordinator::hasQuarantine() const
{
    auto &slot = faultSlot();
    std::lock_guard<std::mutex> lock(slot.mutex);
    return static_cast<bool>(slot.core);
}

RetirementCoordinator &coordinator()
{
    static auto *instance = new RetirementCoordinator();
    return *instance;
}
}
