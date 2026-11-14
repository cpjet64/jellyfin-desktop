// Private ABI for the pinned Qt Quick recovery build; not a public Qt API.
#ifndef QT_RECOVERY_SUBMISSION_ABI_H
#define QT_RECOVERY_SUBMISSION_ABI_H
#include <stdint.h>
#include <QtQuick/qtquickglobal.h>
#ifndef QT_RECOVERY_ABI_H
#include <QtGui/private/QtRecoveryAbi.h>
#endif

#define QT_RECOVERY_SUBMISSION_ABI_VERSION 1u
#define QT_RECOVERY_QUICK_SOURCE_ID "qtdeclarative-c1640ba76059882e1d2bc978b6904a19333be4a9-submission-v1"
#if defined(QT_RECOVERY_UNIT_TEST)
#define QT_RECOVERY_QUICK_API
#else
#define QT_RECOVERY_QUICK_API Q_QUICK_EXPORT
#endif
typedef struct QtRecoveryFrameV1 {
    uint32_t size, version;
    uint64_t attempt, renderer_generation, entry_id, discontinuity_id;
    uint64_t source_frame_serial, render_serial;
    double media_pts;
    uint32_t actual_render_fbo, reserved;
} QtRecoveryFrameV1;
typedef struct QtRecoverySubmissionV1 {
    uint32_t size, version;
    QtRecoverySwapTicketV1 ticket;
    QtRecoveryFrameV1 frame;
    uint64_t node_lifetime, texture_generation, fbo_content_serial;
    uint32_t display_texture, reserved;
} QtRecoverySubmissionV1;
#ifdef __cplusplus
extern "C" {
#endif
QT_RECOVERY_QUICK_API const char *qt_recovery_quick_source_id_v1(void);
QT_RECOVERY_QUICK_API int qt_recovery_publish_native_frame_v1(const QtRecoveryFrameV1 *, QtRecoverySwapTicketV1 *);
QT_RECOVERY_QUICK_API int qt_recovery_take_submission_v1(uint64_t, uint64_t, uint64_t, QtRecoverySubmissionV1 *);
QT_RECOVERY_QUICK_API void qt_recovery_revoke_v1(uint64_t);
#ifdef __cplusplus
}
#endif
#endif
