#include "RendererOwners.h"

namespace RendererRuntime {

static std::shared_ptr<GenerationOwner> &quarantined();

GenerationOwner::GenerationOwner(quint64 id, std::shared_ptr<CoreOwner> coreLease)
    : generation(id), core(std::move(coreLease))
{
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
    if (!context || !originalThread || !originalContext ||
        QThread::currentThread() != originalThread ||
        QOpenGLContext::currentContext() != originalContext || !originalContext->isValid()) {
        blocked.store(true, std::memory_order_release);
        return false;
    }
    mpv_render_context_set_update_callback(context, nullptr, nullptr);
    if (callbacks.load(std::memory_order_acquire) != 0) {
        blocked.store(true, std::memory_order_release);
        return false;
    }
    mpv_render_context_free(context);
    context = nullptr;
    retired.store(true, std::memory_order_release);
    return true;
}

bool RetirementCoordinator::retain(const std::shared_ptr<GenerationOwner> &owner)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!owner || retained || quarantined()) return false;
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
