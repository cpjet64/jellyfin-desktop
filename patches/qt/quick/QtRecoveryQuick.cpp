#include "QtRecoveryQuick_p.h"
#include <atomic>
#include <cmath>
#include <limits>
#include <QtQuick/QSGTexture>

namespace {
std::atomic<uint64_t> nextIdentity{1};
struct State {
    QtRecoverySwapTicketV1 ticket{};
    QtRecoveryFrameV1 frame{};
    uint64_t node = 0, texture = 0, content = 0;
    uint32_t renderFbo = 0, displayTexture = 0;
    bool active = false, inFbo = false, published = false, associated = false, drew = false, ambiguous = false;
};
thread_local State state;
thread_local QtRecoverySubmissionV1 completed{};
struct Unsupported {
    QtRecoverySwapTicketV1 ticket{};
    QtRecoveryFrameV1 frame{};
    uint64_t node = 0;
};
thread_local Unsupported unsupported;
struct NodeRegistration {
    const void *node = nullptr;
    uint64_t lifetime = 0, window = 0, scene = 0;
    const void *textureWrapper = nullptr;
    QtRecoveryFrameV1 frame{};
    uint64_t textureGeneration = 0, content = 0;
    uint32_t displayTexture = 0;
};
thread_local NodeRegistration nodes[32];

NodeRegistration *findNode(uint64_t lifetime)
{
    for (auto &entry : nodes) {
        if (entry.node && entry.lifetime == lifetime)
            return &entry;
    }
    return nullptr;
}

bool validFrame(const QtRecoveryFrameV1 &frame)
{
    const auto limit = uint64_t(std::numeric_limits<int64_t>::max());
    return frame.size == sizeof(frame) && frame.version == QT_RECOVERY_SUBMISSION_ABI_VERSION
            && !frame.reserved && frame.attempt <= limit
            && frame.renderer_generation && frame.renderer_generation <= limit
            && frame.entry_id && frame.entry_id <= limit
            && frame.discontinuity_id && frame.discontinuity_id <= limit
            && frame.source_frame_serial && frame.source_frame_serial <= limit
            && frame.render_serial && frame.render_serial <= limit
            && std::isfinite(frame.media_pts) && frame.media_pts >= 0.0
            && frame.media_pts <= double(std::numeric_limits<int64_t>::max() / 2000);
}
}

void qtRecoveryRegisterNode(const void *node, uint64_t lifetime)
{
    if (!node || !lifetime)
        return;
    for (const auto &entry : nodes) {
        if (entry.node == node)
            return;
    }
    for (auto &entry : nodes) {
        if (!entry.node) {
            entry.node = node;
            entry.lifetime = lifetime;
            return;
        }
    }
}

void qtRecoveryInvalidateNode(uint64_t lifetime)
{
    if (unsupported.node == lifetime)
        unsupported = {};
    if (completed.node_lifetime == lifetime)
        completed = {};
    if (NodeRegistration *entry = findNode(lifetime)) {
        entry->window = entry->scene = entry->textureGeneration = entry->content = 0;
        entry->displayTexture = 0;
        entry->textureWrapper = nullptr;
        entry->frame = {};
    }
    if (state.node == lifetime)
        qtRecoveryFboAbort();
}

void qtRecoveryUnregisterNode(const void *node, uint64_t lifetime)
{
    if (unsupported.node == lifetime)
        unsupported = {};
    if (completed.node_lifetime == lifetime)
        completed = {};
    for (auto &entry : nodes) {
        if (entry.node == node && entry.lifetime == lifetime) {
            entry = {};
            if (state.node == lifetime)
                qtRecoveryFboAbort();
            return;
        }
    }
}

uint64_t qtRecoveryNodeIdentity(const void *node)
{
    for (const auto &entry : nodes) {
        if (entry.node == node)
            return entry.lifetime;
    }
    return 0;
}

void qtRecoveryMarkDraw(uint64_t nodeLifetime, uintptr_t renderTarget, const void *boundTexture, const void *boundRhiTexture,
                        int sampleCount, bool normalColorPass)
{
    const NodeRegistration *entry = findNode(nodeLifetime);
    if (!state.active || !entry || !normalColorPass || !entry->content || !boundTexture || !boundRhiTexture
        || entry->textureWrapper != boundTexture || entry->window != state.ticket.window_lifetime
        || entry->scene != state.ticket.scenegraph_generation || renderTarget != state.ticket.render_target
        || static_cast<const QSGTexture *>(boundTexture)->rhiTexture() != boundRhiTexture)
        return;
    if (sampleCount != 1) {
        if (sampleCount > 1)
            unsupported = {state.ticket, entry->frame, nodeLifetime};
        return;
    }
    if (state.drew && state.node != nodeLifetime) {
        state.ambiguous = true;
        return;
    }
    state.node = nodeLifetime;
    state.frame = entry->frame;
    state.texture = entry->textureGeneration;
    state.content = entry->content;
    state.displayTexture = entry->displayTexture;
    state.associated = state.drew = true;
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
    completed = {};
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
    const bool swapSucceeded = state.active
            && qt_recovery_swap_finish_v1(&state.ticket, endSucceeded, skipPresent);
    if (swapSucceeded && endSucceeded && !skipPresent && state.associated && state.drew
        && !state.ambiguous && state.content && state.texture && state.displayTexture) {
        completed.size = sizeof(completed);
        completed.version = QT_RECOVERY_SUBMISSION_ABI_VERSION;
        completed.ticket = state.ticket;
        completed.frame = state.frame;
        completed.node_lifetime = state.node;
        completed.texture_generation = state.texture;
        completed.fbo_content_serial = state.content;
        completed.display_texture = state.displayTexture;
        if (unsupported.ticket.window_lifetime == completed.ticket.window_lifetime
            && unsupported.frame.renderer_generation == completed.frame.renderer_generation
            && unsupported.ticket.scenegraph_generation == completed.ticket.scenegraph_generation)
            unsupported = {};
    }
    state = {};
}

void qtRecoveryFboBegin(uint32_t fbo, uint64_t nodeLifetime)
{
    qtRecoveryInvalidateNode(nodeLifetime);
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
    state.inFbo = state.published = state.associated = state.drew = false;
    state.renderFbo = 0;
    state.node = state.texture = state.content = 0;
    state.displayTexture = 0;
}

void qtRecoveryFboUnsupported()
{
    if (state.active && state.inFbo && state.published && findNode(state.node))
        unsupported = {state.ticket, state.frame, state.node};
    qtRecoveryFboAbort();
}

void qtRecoveryFboEnd(uint32_t displayTexture, uint64_t textureGeneration, const void *textureWrapper)
{
    NodeRegistration *entry = findNode(state.node);
    if (!state.inFbo || !state.published || !displayTexture || !textureGeneration || !textureWrapper || !entry) {
        qtRecoveryFboAbort();
        return;
    }
    state.inFbo = false;
    state.displayTexture = displayTexture;
    state.texture = textureGeneration;
    state.content = qtRecoveryIdentity();
    state.associated = state.content != 0;
    if (state.associated) {
        entry->window = state.ticket.window_lifetime;
        entry->scene = state.ticket.scenegraph_generation;
        entry->frame = state.frame;
        entry->textureWrapper = textureWrapper;
        entry->textureGeneration = textureGeneration;
        entry->displayTexture = displayTexture;
        entry->content = state.content;
    }
}

extern "C" int qt_recovery_publish_native_frame_v1(const QtRecoveryFrameV1 *frame,
                                                    QtRecoverySwapTicketV1 *ticket)
{
    if (!frame || !ticket || !state.active || !state.inFbo || state.published
        || !validFrame(*frame)
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

extern "C" const char *qt_recovery_quick_bound_gui_source_id_v1()
{
    return qt_recovery_swap_source_id_v1();
}

extern "C" int qt_recovery_take_submission_v1(uint64_t window, uint64_t renderer, uint64_t attempt,
                                               QtRecoverySubmissionV1 *result)
{
    if (!result || result->size != sizeof(*result) || result->version != QT_RECOVERY_SUBMISSION_ABI_VERSION
        || completed.size != sizeof(completed) || !window || !renderer
        || completed.ticket.window_lifetime != window
        || completed.frame.renderer_generation != renderer || completed.frame.attempt != attempt)
        return 0;
    *result = completed;
    completed = {};
    return 1;
}

extern "C" int qt_recovery_take_unsupported_v1(uint64_t window, uint64_t renderer, uint64_t scene)
{
    if (!window || !renderer || !scene || unsupported.ticket.window_lifetime != window
        || unsupported.frame.renderer_generation != renderer || unsupported.ticket.scenegraph_generation != scene)
        return 0;
    unsupported = {};
    return 1;
}

extern "C" void qt_recovery_revoke_v1(uint64_t windowLifetime)
{
    if (unsupported.ticket.window_lifetime == windowLifetime)
        unsupported = {};
    if (completed.ticket.window_lifetime == windowLifetime)
        completed = {};
    for (auto &entry : nodes) {
        if (entry.window == windowLifetime)
            qtRecoveryInvalidateNode(entry.lifetime);
    }
    if (state.active && state.ticket.window_lifetime == windowLifetime)
        qtRecoveryAbort();
}
