#pragma once

#include <QPointer>
#include <QObject>
#include <QOpenGLContext>
#include <QThread>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <mpv/client.h>
#include <mpv/render.h>

class MpvController;

namespace RendererRuntime {

struct CoreOwner;
enum class AdmissionStatus { Reserved, Deferred, Quarantined, Busy };
struct AdmissionResult { AdmissionStatus status; quint64 token; };
using AdmissionContinuation = std::function<void(AdmissionStatus, quint64, quint64)>;
// Dispatcher is GUI-thread owned and must be closed with closeAdmissionDispatcher
// on that same thread before destruction. Continuation validates receiver/revision.
AdmissionResult reserveCore(QObject *dispatcher, quint64 revision, AdmissionContinuation continuation);
void cancelCoreReservation(quint64 token);
void closeAdmissionDispatcher(QObject *dispatcher);
class NativeLease {
public:
    NativeLease() = default;
    NativeLease(NativeLease &&other) noexcept;
    NativeLease &operator=(NativeLease &&other) noexcept;
    NativeLease(const NativeLease &) = delete;
    NativeLease &operator=(const NativeLease &) = delete;
    ~NativeLease();
    mpv_handle *get() const;
    void contain();
    explicit operator bool() const { return static_cast<bool>(owner); }
private:
    explicit NativeLease(std::shared_ptr<CoreOwner> owner);
    friend struct CoreOwner;
    std::shared_ptr<CoreOwner> owner;
};

class WakeRelay : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void notify() { emit nativeEventsAvailable(); }
signals:
    void nativeEventsAvailable();
    void renderUpdateRequested(quint64 generation);
};

// Native destruction is explicit and must be dispatched after external fences.
struct CoreOwner : std::enable_shared_from_this<CoreOwner> {
    enum class QuarantineStopQueue { NotRequested, Queued, Rejected };
    static constexpr uint64_t quarantineStopUserdata = uint64_t{7} << 61;
    static constexpr int stopResultPending = 1;
    mpv_handle *handle = nullptr;
    MpvController *controller = nullptr; // owned until explicit finalization
    QThread *worker = nullptr; // owned until explicit finalization
    WakeRelay *wakeRelay = nullptr; // owned on worker until queued fence
    std::atomic<bool> commandAuthority{true};
    std::atomic<bool> finalizationRequested{false};
    std::atomic<bool> finalizationQueued{false};
    std::atomic<bool> faultQuarantined{false};
    std::atomic<unsigned> admittedControl{0};
    std::atomic<unsigned> liveGenerations{0};
    std::atomic<unsigned> externalConsumers{0};
    std::atomic<bool> wakeAdmission{false};
    std::atomic<bool> wakeClosed{false};
    std::atomic<unsigned> wakeCallbacks{0};
    std::mutex postingMutex;
    std::mutex wakeRegistrationMutex;
    std::atomic<bool> quarantineStopStarted{false};
    std::atomic<QuarantineStopQueue> quarantineStopQueue{QuarantineStopQueue::NotRequested};
    std::atomic<int> quarantineStopNativeResult{stopResultPending};

    bool enterControl();
    void leaveControl();
    NativeLease acquireNative();
    bool postControl(std::function<void(mpv_handle *)> operation);
    bool bindExternalConsumer();
    void unbindExternalConsumer();
    void revokeControl();
    bool installExternalWakeup();
    void closeExternalWakeup();
    bool requestFinalization();
    void requestQuarantineStop();
    void blockUnresolvedHook();
    void maybeFinalize();
    static std::shared_ptr<CoreOwner> adoptReserved(quint64 token, mpv_handle *handle,
        MpvController *controller, QThread *worker, WakeRelay *relay);
private:
    CoreOwner() = default;
};

struct GenerationOwner : std::enable_shared_from_this<GenerationOwner> {
    static std::shared_ptr<GenerationOwner> create(quint64 id, const std::shared_ptr<CoreOwner> &core);
    const quint64 generation;
    const std::shared_ptr<CoreOwner> core;
    std::atomic<bool> callbackAdmission{true};
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> observerAuthority{true};
    std::atomic<bool> retired{false};
    std::atomic<bool> blocked{false};
    mpv_render_context *context = nullptr; // render thread only
    bool contextEverCreated = false; // render thread only
    QPointer<QOpenGLContext> originalContext;
    QPointer<QThread> originalThread;

    bool enterCallback();
    void leaveCallback();
    void revoke();
    void notifyRenderUpdate();
    bool retireOnOriginalContext();
private:
    GenerationOwner(quint64 id, std::shared_ptr<CoreOwner> coreLease);
};

// Coordinator retains live generations until retirement or quarantine.
class RetirementCoordinator {
public:
    bool retain(const std::shared_ptr<GenerationOwner> &owner);
    bool releaseRetired(const std::shared_ptr<GenerationOwner> &owner);
    bool quarantine(const std::shared_ptr<GenerationOwner> &owner);
    bool hasQuarantine() const;

private:
    RetirementCoordinator() = default;
    friend RetirementCoordinator &coordinator();
    mutable std::mutex mutex;
    std::shared_ptr<GenerationOwner> retained;
};

RetirementCoordinator &coordinator();
}
