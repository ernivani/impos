#ifndef _KERNEL_DRM_H
#define _KERNEL_DRM_H

#include <stdint.h>
#include <kernel/ioctl.h>

/*
 * DRM (Direct Rendering Manager) subsystem for ImposOS.
 *
 * Stage 0: ioctl dispatch + DRM_IOCTL_VERSION only.
 * Later stages add KMS (mode setting), GEM (buffer management),
 * and driver-specific ioctls (VirtIO GPU 3D, Intel i915).
 *
 * DRM ioctl type magic = 'd' (0x64), matching Linux.
 */

#define DRM_IOCTL_BASE  'd'

/* ── DRM ioctl command numbers ──────────────────────────────────── */

#define DRM_IOCTL_VERSION           _IOWR(DRM_IOCTL_BASE, 0x00, sizeof(drm_version_t))
#define DRM_IOCTL_GET_CAP           _IOWR(DRM_IOCTL_BASE, 0x0C, sizeof(drm_get_cap_t))
#define DRM_IOCTL_SET_CLIENT_CAP    _IOW(DRM_IOCTL_BASE, 0x0D, sizeof(drm_set_client_cap_t))

/* KMS ioctls — Stage 1 placeholders */
#define DRM_IOCTL_MODE_GETRESOURCES _IOWR(DRM_IOCTL_BASE, 0xA0, sizeof(drm_mode_card_res_t))

/* ── DRM capability IDs ─────────────────────────────────────────── */

#define DRM_CAP_DUMB_BUFFER         0x01
#define DRM_CAP_PRIME               0x02
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x06

#define DRM_CLIENT_CAP_UNIVERSAL_PLANES  2
#define DRM_CLIENT_CAP_ATOMIC            3

/* ── DRM structures ─────────────────────────────────────────────── */

/* DRM_IOCTL_VERSION */
typedef struct {
    int32_t  version_major;
    int32_t  version_minor;
    int32_t  version_patchlevel;
    uint32_t name_len;          /* in: buffer size, out: actual length */
    char    *name;              /* driver name (e.g. "impos-gpu") */
    uint32_t date_len;
    char    *date;              /* driver date string */
    uint32_t desc_len;
    char    *desc;              /* driver description */
} drm_version_t;

/* DRM_IOCTL_GET_CAP */
typedef struct {
    uint64_t capability;
    uint64_t value;
} drm_get_cap_t;

/* DRM_IOCTL_SET_CLIENT_CAP */
typedef struct {
    uint64_t capability;
    uint64_t value;
} drm_set_client_cap_t;

/* DRM_IOCTL_MODE_GETRESOURCES (Stage 1 placeholder) */
typedef struct {
    uint32_t *fb_id_ptr;
    uint32_t *crtc_id_ptr;
    uint32_t *connector_id_ptr;
    uint32_t *encoder_id_ptr;
    uint32_t  count_fbs;
    uint32_t  count_crtcs;
    uint32_t  count_connectors;
    uint32_t  count_encoders;
    uint32_t  min_width;
    uint32_t  max_width;
    uint32_t  min_height;
    uint32_t  max_height;
} drm_mode_card_res_t;

/* ── DRM device state ───────────────────────────────────────────── */

typedef struct {
    int      initialized;
    /* Stage 1 will add: CRTCs, connectors, encoders, mode list */
    /* Stage 2 will add: GEM handle table, framebuffer list */
} drm_device_t;

/* ── DRM API ────────────────────────────────────────────────────── */

/* Initialize the DRM subsystem (called once at boot) */
void drm_init(void);

/* Handle an ioctl on an open DRM fd. Returns 0 on success, -errno on error. */
int drm_ioctl(uint32_t cmd, void *arg);

/* Query if DRM is available */
int drm_is_available(void);

#endif
