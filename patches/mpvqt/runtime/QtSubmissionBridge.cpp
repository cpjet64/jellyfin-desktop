#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif
#include "QtSubmissionBridge.h"
#include <QQuickWindow>
#include <cstddef>
#include <cstring>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

static_assert(sizeof(uintptr_t) != 8 || sizeof(QtRecoverySwapTicketV1) == 72, "unexpected Qt recovery ticket layout");
static_assert(sizeof(QtRecoveryFrameV1) == 72, "unexpected Qt recovery frame layout");
static_assert(sizeof(uintptr_t) != 8 || sizeof(QtRecoverySubmissionV1) == 184, "unexpected Qt recovery submission layout");
static_assert(offsetof(QtRecoveryFrameV1, render_serial) == 48, "unexpected render serial offset");

namespace QtSubmissionBridge {
namespace {
#if defined(_WIN32)
HMODULE moduleForAddress(FARPROC address)
{
    HMODULE module = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(address), &module) ? module : nullptr;
}
#endif
}

Resolver::Resolver()
{
#if !defined(JELLYFIN_VERIFIED_RECOVERY_KIT) || !JELLYFIN_VERIFIED_RECOVERY_KIT
    return;
#endif
    if (sizeof(uintptr_t) != 8) return;
#if defined(_WIN32)
    const auto anchor = reinterpret_cast<FARPROC>(&QQuickWindow::setGraphicsApi);
    const auto module = moduleForAddress(anchor);
    if (!module) return;
    auto symbol = [module](const char *name) -> FARPROC {
        const auto address = GetProcAddress(module, name);
        return address && moduleForAddress(address) == module ? address : nullptr;
    };
#elif (defined(__unix__) || defined(__APPLE__)) && defined(RTLD_NOLOAD)
    const auto anchor = reinterpret_cast<void *>(&QQuickWindow::setGraphicsApi);
    Dl_info origin{};
    if (!dladdr(anchor, &origin) || !origin.dli_fname || !origin.dli_fbase) return;
    moduleLease_ = dlopen(origin.dli_fname, RTLD_NOLOAD | RTLD_NOW | RTLD_LOCAL);
    if (!moduleLease_) return;
    auto symbol = [this, &origin](const char *name) -> void * {
        void *address = dlsym(moduleLease_, name);
        Dl_info info{};
        return address && dladdr(address, &info) && info.dli_fbase == origin.dli_fbase ? address : nullptr;
    };
#else
    return;
#endif
#if defined(_WIN32) || ((defined(__unix__) || defined(__APPLE__)) && defined(RTLD_NOLOAD))
    const auto source = reinterpret_cast<const char *(*)()>(symbol("qt_recovery_quick_source_id_v1"));
    if (!source || !source() || std::strcmp(source(), QT_RECOVERY_QUICK_SOURCE_ID) != 0) return;
    const auto guiSource = reinterpret_cast<const char *(*)()>(symbol("qt_recovery_quick_bound_gui_source_id_v1"));
    if (!guiSource || !guiSource() || std::strcmp(guiSource(), QT_RECOVERY_SWAP_SOURCE_ID) != 0) return;
    publish_ = reinterpret_cast<decltype(publish_)>(symbol("qt_recovery_publish_native_frame_v1"));
    take_ = reinterpret_cast<decltype(take_)>(symbol("qt_recovery_take_submission_v1"));
    revoke_ = reinterpret_cast<decltype(revoke_)>(symbol("qt_recovery_revoke_v1"));
    unsupported_ = reinterpret_cast<decltype(unsupported_)>(symbol("qt_recovery_take_unsupported_v1"));
    if (!publish_ || !take_ || !revoke_ || !unsupported_) publish_ = nullptr;
#endif
}

Resolver::~Resolver()
{
#if (defined(__unix__) || defined(__APPLE__)) && defined(RTLD_NOLOAD)
    if (moduleLease_) dlclose(moduleLease_);
#endif
}
bool Resolver::available() const noexcept { return publish_ && take_ && revoke_ && unsupported_; }
bool Resolver::publish(const QtRecoveryFrameV1 &frame, QtRecoverySwapTicketV1 &ticket) const
{
    return available() && publish_(&frame, &ticket) == 1;
}
bool Resolver::take(uint64_t window, uint64_t generation, uint64_t attempt, QtRecoverySubmissionV1 &out) const
{
    return available() && take_(window, generation, attempt, &out) == 1;
}
void Resolver::revoke(uint64_t window) const { if (available() && window) revoke_(window); }
bool Resolver::takeUnsupported(uint64_t window, uint64_t generation, uint64_t sceneGeneration) const
{
    return available() && unsupported_(window, generation, sceneGeneration) == 1;
}
}
