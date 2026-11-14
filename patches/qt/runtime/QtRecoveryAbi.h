// Private bridge for the pinned Qt recovery build. Not a Qt public API.
#ifndef QT_RECOVERY_ABI_H
#define QT_RECOVERY_ABI_H
#include <stdint.h>
#include <QtGui/qtguiglobal.h>

#define QT_RECOVERY_SWAP_ABI_VERSION 1u
#define QT_RECOVERY_SWAP_SOURCE_ID "qtbase-be09b211db70e1b0155d05c18668ad76e7e3df51-swap-v1"
typedef struct QtRecoverySwapTicketV1 {
    uint32_t size;
    uint32_t version;
    uint64_t window_lifetime;
    uint64_t scenegraph_generation;
    uint64_t frame_serial;
    uintptr_t render_thread;
    uintptr_t swapchain;
    uintptr_t render_target;
    uintptr_t surface;
    uintptr_t platform_context;
} QtRecoverySwapTicketV1;

#ifdef __cplusplus
extern "C" {
#endif
Q_GUI_EXPORT const char *qt_recovery_swap_source_id_v1(void);
Q_GUI_EXPORT int qt_recovery_swap_begin_v1(const QtRecoverySwapTicketV1 *ticket);
Q_GUI_EXPORT int qt_recovery_swap_finish_v1(const QtRecoverySwapTicketV1 *ticket,
                                             int qt_end_success, int skip_present);
Q_GUI_EXPORT void qt_recovery_swap_abort_v1(const QtRecoverySwapTicketV1 *ticket);
Q_GUI_EXPORT void qt_recovery_swap_report_v1(uintptr_t surface, uintptr_t platform_context,
                                              int desktop_wgl, int double_buffered,
                                              int swap_succeeded);
#ifdef __cplusplus
}
#endif
#endif
