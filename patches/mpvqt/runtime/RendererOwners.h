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

class WakeRelay : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    void notify() { emit nativeEventsAvailable(); }
signals:
    void nativeEventsAvailable();
};

// Native destruction is explicit and must be dispatched after external fences.
struct CoreOwner : std::enable_shared_from_this<CoreOwner> {
    mpv_handle *handle = nullptr;
    MpvController *controller = nullptr; // owned until explicit finalization
    QThread *worker = nullptr; // owned until explicit finalization
    WakeRelay *wakeRelay = nullptr; // owned on worker until queued fence
    std::atomic<bool> commandAuthority{true};
    std::atomic<bool> finalizationRequested{false};
    std::atomic<unsigned> admittedControl{0};
    std::atomic<unsigned> liveGenerations{0};
    std::atomic<bool> wakeAdmission{false};
    std::atomic<unsigned> wakeCallbacks{0};
    std::mutex postingMutex;

    bool enterControl();
    void leaveControl();
    bool postControl(std::function<void(mpv_handle *)> operation);
    void revokeControl();
    bool installExternalWakeup();
    void closeExternalWakeup();
    bool requestFinalization();
};

struct GenerationOwner {
    GenerationOwner(quint64 id, std::shared_ptr<CoreOwner> coreLease);
    const quint64 generation;
    const std::shared_ptr<CoreOwner> core;
    std::atomic<bool> callbackAdmission{true};
    std::atomic<unsigned> callbacks{0};
    std::atomic<bool> observerAuthority{true};
    std::atomic<bool> retired{false};
    std::atomic<bool> blocked{false};
    mpv_render_context *context = nullptr; // render thread only
    QPointer<QOpenGLContext> originalContext;
    QPointer<QThread> originalThread;

    bool enterCallback();
    void leaveCallback();
    void revoke();
    bool retireOnOriginalContext();
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
