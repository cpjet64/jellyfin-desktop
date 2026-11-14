#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "RecoveryFrameBridge.h"

#ifdef MPV_CPLUGIN_DYNAMIC_SYM
#error RecoveryFrameBridge requires the normal linked mpv API, not the C plugin symbol table
#endif

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

static_assert(sizeof(RecoveryFrameObservationV1) == 56, "unexpected recovery ABI size");
static_assert(offsetof(RecoveryFrameObservationV1, playlist_entry_id) == 16, "unexpected entry offset");
static_assert(offsetof(RecoveryFrameObservationV1, discontinuity_id) == 24, "unexpected discontinuity offset");
static_assert(offsetof(RecoveryFrameObservationV1, source_frame_serial) == 32, "unexpected source offset");
static_assert(offsetof(RecoveryFrameObservationV1, render_serial) == 40, "unexpected render offset");
static_assert(offsetof(RecoveryFrameObservationV1, media_pts) == 48, "unexpected PTS offset");
static_assert(sizeof(uint64_t) == 8 && sizeof(double) == 8, "unexpected recovery ABI widths");

namespace RecoveryFrameBridge {
namespace {

constexpr const char *recoveryName = "mpv_render_context_render_recovery_v1";
constexpr const char *standardName = "mpv_render_context_render";

#if defined(_WIN32)
HMODULE moduleForAddress(FARPROC address)
{
    HMODULE module = nullptr;
    const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
    return GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(address), &module)
        ? module : nullptr;
}

RecoveryRenderV1 resolveRecovery(void *&lease)
{
    (void)lease; // The existing mpv owner retains the module, not this handle.
    const FARPROC standard = reinterpret_cast<FARPROC>(&mpv_render_context_render);
    const HMODULE module = moduleForAddress(standard);
    if (!module || GetProcAddress(module, standardName) != standard)
        return nullptr;
    const FARPROC candidate = GetProcAddress(module, recoveryName);
    if (!candidate || moduleForAddress(candidate) != module)
        return nullptr;
    return reinterpret_cast<RecoveryRenderV1>(candidate);
}
#elif (defined(__unix__) || defined(__APPLE__)) && defined(RTLD_NOLOAD)
RecoveryRenderV1 resolveRecovery(void *&lease)
{
    const auto standard = reinterpret_cast<void *>(&mpv_render_context_render);
    Dl_info origin{};
    if (!dladdr(standard, &origin) || !origin.dli_fname || !origin.dli_fbase)
        return nullptr;
    void *handle = dlopen(origin.dli_fname, RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL);
    if (!handle)
        return nullptr;
    void *foundStandard = dlsym(handle, standardName);
    void *candidate = dlsym(handle, recoveryName);
    Dl_info candidateInfo{};
    if (foundStandard != standard || !candidate || !dladdr(candidate, &candidateInfo) ||
        candidateInfo.dli_fbase != origin.dli_fbase) {
        dlclose(handle);
        return nullptr;
    }
    lease = handle;
    return reinterpret_cast<RecoveryRenderV1>(candidate);
}
#else
RecoveryRenderV1 resolveRecovery(void *&lease)
{
    (void)lease;
    return nullptr;
}
#endif

RecoveryFrameObservationV1 emptyObservation()
{
    RecoveryFrameObservationV1 observation{};
    observation.size = sizeof(observation);
    observation.version = RECOVERY_FRAME_OBSERVATION_VERSION;
    return observation;
}

bool validObservation(const RecoveryFrameObservationV1 &observation)
{
    return observation.size == sizeof(observation) &&
           observation.version == RECOVERY_FRAME_OBSERVATION_VERSION &&
           observation.valid == 1 && observation.reserved == 0 &&
           observation.playlist_entry_id > 0 &&
           observation.playlist_entry_id <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) &&
           observation.discontinuity_id && observation.source_frame_serial &&
           observation.render_serial && std::isfinite(observation.media_pts) &&
           observation.media_pts >= 0.0 &&
           observation.media_pts <= static_cast<double>(std::numeric_limits<int64_t>::max() / 2000);
}

bool skipRequested(const mpv_render_param *params)
{
    for (auto param = params; param && param->type != MPV_RENDER_PARAM_INVALID; ++param) {
        if (param->type == MPV_RENDER_PARAM_SKIP_RENDERING)
            return !param->data || *static_cast<const int *>(param->data) != 0;
    }
    return false;
}

} // namespace

Resolver::Resolver()
{
    recoveryRender_ = resolveRecovery(moduleLease_);
}

Resolver::~Resolver()
{
#if (defined(__unix__) || defined(__APPLE__)) && defined(RTLD_NOLOAD)
    if (moduleLease_)
        dlclose(moduleLease_);
#endif
}

bool Resolver::available() const noexcept
{
    return recoveryRender_ != nullptr;
}

RenderResult Resolver::render(mpv_render_context *context, mpv_render_param *params) const
{
    RenderResult result{0, available(), emptyObservation()};
    if (recoveryRender_)
        result.error = recoveryRender_(context, params, &result.observation);
    else
        result.error = mpv_render_context_render(context, params);
    if (result.error < 0 || skipRequested(params) || !validObservation(result.observation))
        result.observation = emptyObservation();
    return result;
}

} // namespace RecoveryFrameBridge
