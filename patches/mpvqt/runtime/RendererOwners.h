#pragma once

#include <QPointer>
#include <QOpenGLContext>
#include <QThread>
#include <atomic>
#include <memory>
#include <mutex>
#include <mpv/client.h>
#include <mpv/render.h>

class MpvController;

namespace RendererRuntime {

// Native destruction is explicit and must be dispatched after external fences.
struct CoreOwner {
    mpv_handle *handle = nullptr;
    QPointer<MpvController> controller;
    QPointer<QThread> worker;
    std::atomic<bool> commandAuthority{true};
    std::atomic<bool> finalizationRequested{false};
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
    mutable std::mutex mutex;
    std::shared_ptr<GenerationOwner> retained;
};

RetirementCoordinator &coordinator();
}
