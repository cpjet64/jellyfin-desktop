#ifndef JELLYFIN_RECOVERY_FRAME_OBSERVATION_H
#define JELLYFIN_RECOVERY_FRAME_OBSERVATION_H

#include <stdint.h>

/*
 * Version 1 is optional. Resolve mpv_render_context_render_recovery_v1 from
 * the same loaded module as the existing mpv API. Absence means unavailable.
 * A successful call with valid == 1 describes the frame selected and rendered
 * by that call. It does not prove Qt submission, swap, or visible pixels.
 * The caller initializes size and version. Other fields are output only.
 * Reject entry IDs outside 1..INT64_MAX before matching mpv start-file events.
 * Compare source_frame_serial, not render_serial, to detect source progress.
 * A redraw retains the selected source identity and may have a new render serial.
 */
#define RECOVERY_FRAME_OBSERVATION_VERSION 1u

typedef struct RecoveryFrameObservationV1 {
    uint32_t size;                 /* caller sets sizeof(RecoveryFrameObservationV1) */
    uint32_t version;              /* caller sets RECOVERY_FRAME_OBSERVATION_VERSION */
    uint32_t valid;                /* set only after a successful actual render */
    uint32_t reserved;             /* zero */
    uint64_t playlist_entry_id;    /* native mpv playlist entry, never GUI attempt */
    uint64_t discontinuity_id;     /* changes on native video reset/seek */
    uint64_t source_frame_serial;  /* stable across references and redraws */
    uint64_t render_serial;        /* successful observed render, not source progress */
    double media_pts;              /* selected source frame media time, seconds */
} RecoveryFrameObservationV1;

struct mpv_render_context;
struct mpv_render_param;
typedef int (*RecoveryRenderV1)(struct mpv_render_context *,
                                struct mpv_render_param *,
                                RecoveryFrameObservationV1 *);

#define RECOVERY_FRAME_OBSERVATION_VERSION_2 2u
typedef struct RecoveryFrameObservationV2 {
    uint32_t size, version, valid, reserved;
    uint64_t playlist_entry_id, discontinuity_id, source_frame_serial, render_serial;
    double media_pts;
    uint64_t operation_id;
} RecoveryFrameObservationV2;

struct mpv_handle;
typedef int (*RecoveryRenderV2)(struct mpv_render_context *, struct mpv_render_param *,
                                RecoveryFrameObservationV2 *);
typedef int (*RecoverySeekAsyncV1)(struct mpv_handle *, uint64_t, uint64_t,
                                   uint64_t, int64_t);
typedef int (*RecoverySeekCancelV1)(struct mpv_handle *);

#endif
