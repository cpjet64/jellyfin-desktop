#pragma once

#include <mpv/render.h>

#include "../../../src/native/RecoveryFrameObservation.h"

namespace RecoveryFrameBridge {

struct RenderResult {
    int error;
    bool capabilityAvailable;
    RecoveryFrameObservationV1 observation;
};

// The native mpv module and its render context must outlive this resolver.
class Resolver final {
public:
    Resolver();
    ~Resolver();
    Resolver(const Resolver &) = delete;
    Resolver &operator=(const Resolver &) = delete;

    bool available() const noexcept;
    RenderResult render(mpv_render_context *context, mpv_render_param *params) const;

private:
    RecoveryRenderV1 recoveryRender_ = nullptr;
    void *moduleLease_ = nullptr;
};

} // namespace RecoveryFrameBridge
