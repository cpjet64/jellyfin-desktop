#include "mpvabstractitem.h"
#include "mpvabstractitem_p.h"

bool MpvAbstractItem::recoveryOwnedSeekAvailable() const
{
#if !defined(JELLYFIN_VERIFIED_RECOVERY_KIT) || !JELLYFIN_VERIFIED_RECOVERY_KIT
    return false;
#else
    return d_ptr->core && d_ptr->seekResolver.ownedSeekAvailable() && recoveryFrameObservationAvailable();
#endif
}

int MpvAbstractItem::dispatchRecoveryOwnedSeek(quint64 reply, quint64 operation, quint64 entry, qint64 target)
{
    auto owner = d_ptr->core;
    auto lease = owner ? owner->acquireNative() : RendererRuntime::NativeLease{};
    return lease && recoveryOwnedSeekAvailable()
        ? d_ptr->seekResolver.seekAsync(lease.get(), reply, operation, entry, target)
        : MPV_ERROR_UNINITIALIZED;
}

int MpvAbstractItem::cancelRecoveryOwnedSeek()
{
#if !defined(JELLYFIN_VERIFIED_RECOVERY_KIT) || !JELLYFIN_VERIFIED_RECOVERY_KIT
    return MPV_ERROR_UNINITIALIZED;
#else
    auto owner = d_ptr->core;
    auto lease = owner ? owner->acquireNative() : RendererRuntime::NativeLease{};
    return lease && d_ptr->seekResolver.ownedSeekAvailable()
        ? d_ptr->seekResolver.cancelSeek(lease.get()) : MPV_ERROR_UNINITIALIZED;
#endif
}
