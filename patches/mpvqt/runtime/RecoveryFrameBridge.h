#pragma once

#include <mpv/render.h>

#include "../../../src/native/RecoveryFrameObservation.h"

namespace RecoveryFrameBridge {

struct RenderResult {
    int error;
    bool capabilityAvailable;
    RecoveryFrameObservationV1 observation;
};

struct RenderResultV2 {
    int error;
    bool capabilityAvailable;
    RecoveryFrameObservationV2 observation;
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
    bool ownedSeekAvailable() const noexcept;
    int seekAsync(mpv_handle *handle, uint64_t replyUserdata, uint64_t operationId,
                  uint64_t expectedEntryId, int64_t targetMs) const;
    int cancelSeek(mpv_handle *handle) const;
    RenderResultV2 renderV2(mpv_render_context *context, mpv_render_param *params) const;

private:
    RecoveryRenderV1 recoveryRender_ = nullptr;
    RecoveryRenderV2 recoveryRenderV2_ = nullptr;
    RecoverySeekAsyncV1 recoverySeek_ = nullptr;
    RecoverySeekCancelV1 recoveryCancel_ = nullptr;
    void *moduleLease_ = nullptr;
};

} // namespace RecoveryFrameBridge
