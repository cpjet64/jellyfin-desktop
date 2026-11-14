#include "QtRecoveryQuick_p.h"
#include <atomic>
#include <cmath>
#include <limits>

namespace {
std::atomic<uint64_t> nextIdentity{1};
struct State {
    QtRecoverySwapTicketV1 ticket{};
    QtRecoveryFrameV1 frame{};
    uint64_t node = 0, texture = 0, content = 0;
    uint32_t renderFbo = 0, displayTexture = 0;
    bool active = false, inFbo = false, published = false, associated = false;
};
thread_local State state;
}

uint64_t qtRecoveryIdentity()
{
    uint64_t value = nextIdentity.load(std::memory_order_relaxed);
    while (value && value != std::numeric_limits<uint64_t>::max()) {
        if (nextIdentity.compare_exchange_weak(value, value + 1, std::memory_order_relaxed))
            return value;
    }
    return 0;
}

bool qtRecoveryBegin(const QtRecoverySwapTicketV1 &ticket)
{
    qtRecoveryAbort();
    if (!ticket.window_lifetime || !ticket.scenegraph_generation || !ticket.frame_serial
        || !qt_recovery_swap_begin_v1(&ticket))
        return false;
    state.ticket = ticket;
    state.active = true;
    return true;
}

void qtRecoveryAbort()
{
    if (state.active)
        qt_recovery_swap_abort_v1(&state.ticket);
    state = {};
}

void qtRecoveryFinish(bool endSucceeded, bool skipPresent)
{
    if (state.active)
        qt_recovery_swap_finish_v1(&state.ticket, endSucceeded, skipPresent);
    // Draw-use proof is not implemented in this source unit. Never publish a
    // completion based only on the FBO and native swap observations.
    state = {};
}

void qtRecoveryFboBegin(uint32_t fbo, uint64_t nodeLifetime)
{
    if (!state.active || state.inFbo || !fbo || !nodeLifetime) {
        qtRecoveryFboAbort();
        return;
    }
    state.inFbo = true;
    state.renderFbo = fbo;
    state.node = nodeLifetime;
    state.published = state.associated = false;
}

void qtRecoveryFboAbort()
{
    state.inFbo = state.published = state.associated = false;
    state.renderFbo = 0;
    state.node = state.texture = state.content = 0;
    state.displayTexture = 0;
}

void qtRecoveryFboEnd(uint32_t displayTexture, uint64_t textureGeneration)
{
    if (!state.inFbo || !state.published || !displayTexture || !textureGeneration) {
        qtRecoveryFboAbort();
        return;
    }
    state.inFbo = false;
    state.displayTexture = displayTexture;
    state.texture = textureGeneration;
    state.content = qtRecoveryIdentity();
    state.associated = state.content != 0;
}

extern "C" int qt_recovery_publish_native_frame_v1(const QtRecoveryFrameV1 *frame,
                                                    QtRecoverySwapTicketV1 *ticket)
{
    if (!frame || !ticket || !state.active || !state.inFbo || state.published
        || frame->size != sizeof(*frame) || frame->version != QT_RECOVERY_SUBMISSION_ABI_VERSION
        || frame->reserved || !frame->renderer_generation || !frame->entry_id
        || !frame->source_frame_serial || !frame->render_serial || !std::isfinite(frame->media_pts)
        || frame->actual_render_fbo != state.renderFbo) {
        qtRecoveryFboAbort();
        return 0;
    }
    state.frame = *frame;
    *ticket = state.ticket;
    state.published = true;
    return 1;
}

extern "C" const char *qt_recovery_quick_source_id_v1()
{
    return QT_RECOVERY_QUICK_SOURCE_ID;
}

extern "C" int qt_recovery_take_submission_v1(uint64_t, uint64_t, uint64_t,
                                               QtRecoverySubmissionV1 *)
{
    // Capability remains unavailable until recorded main-pass draw proof joins.
    return 0;
}

extern "C" void qt_recovery_revoke_v1(uint64_t windowLifetime)
{
    if (state.active && state.ticket.window_lifetime == windowLifetime)
        qtRecoveryAbort();
}
