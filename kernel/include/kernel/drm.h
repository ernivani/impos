#ifndef _KERNEL_DRM_H
#define _KERNEL_DRM_H

#include <stdint.h>
#include <kernel/ioctl.h>

/*
 * DRM (Direct Rendering Manager) subsystem for ImposOS.
 *
 * Stage 0: ioctl dispatch, DRM_IOCTL_VERSION, GET_CAP.
 * Stage 1: KMS modesetting — CRTC/connector/encoder abstractions,
 *          mode queries, SETCRTC for display mode changes.
 * Later stages add GEM (buffer management) and driver-specific ioctls.
 *
 * DRM ioctl type magic = 'd' (0x64), matching Linux.
 */

#define DRM_IOCTL_BASE  'd'

/* ── DRM ioctl command numbers ──────────────────────────────────── */

/* Core ioctls (Stage 0) */
#define DRM_IOCTL_VERSION           _IOWR(DRM_IOCTL_BASE, 0x00, sizeof(drm_version_t))
#define DRM_IOCTL_GET_CAP           _IOWR(DRM_IOCTL_BASE, 0x0C, sizeof(drm_get_cap_t))
#define DRM_IOCTL_SET_CLIENT_CAP    _IOW(DRM_IOCTL_BASE, 0x0D, sizeof(drm_set_client_cap_t))

/* KMS ioctls (Stage 1) */
#define DRM_IOCTL_MODE_GETRESOURCES _IOWR(DRM_IOCTL_BASE, 0xA0, sizeof(drm_mode_card_res_t))
#define DRM_IOCTL_MODE_GETCRTC      _IOWR(DRM_IOCTL_BASE, 0xA1, sizeof(drm_mode_crtc_t))
#define DRM_IOCTL_MODE_SETCRTC      _IOWR(DRM_IOCTL_BASE, 0xA2, sizeof(drm_mode_crtc_t))
#define DRM_IOCTL_MODE_GETENCODER   _IOWR(DRM_IOCTL_BASE, 0xA6, sizeof(drm_mode_get_encoder_t))
#define DRM_IOCTL_MODE_GETCONNECTOR _IOWR(DRM_IOCTL_BASE, 0xA7, sizeof(drm_mode_get_connector_t))

/* ── DRM capability IDs ─────────────────────────────────────────── */

#define DRM_CAP_DUMB_BUFFER         0x01
#define DRM_CAP_PRIME               0x02
#define DRM_CAP_TIMESTAMP_MONOTONIC 0x06

#define DRM_CLIENT_CAP_UNIVERSAL_PLANES  2
#define DRM_CLIENT_CAP_ATOMIC            3

/* ── KMS constants ──────────────────────────────────────────────── */

#define DRM_DISPLAY_MODE_LEN    32
#define DRM_MAX_MODES           8

/* Connector types (subset of Linux DRM_MODE_CONNECTOR_*) */
#define DRM_MODE_CONNECTOR_Unknown      0
#define DRM_MODE_CONNECTOR_VGA          1
#define DRM_MODE_CONNECTOR_VIRTUAL      15

/* Encoder types (subset of Linux DRM_MODE_ENCODER_*) */
#define DRM_MODE_ENCODER_NONE       0
#define DRM_MODE_ENCODER_VIRTUAL    7

/* Connection status */
#define DRM_MODE_CONNECTED          1
#define DRM_MODE_DISCONNECTED       2
#define DRM_MODE_UNKNOWNCONNECTION  3

/* Subpixel order */
#define DRM_MODE_SUBPIXEL_UNKNOWN   1

/* Mode type flags */
#define DRM_MODE_TYPE_PREFERRED     (1 << 3)
#define DRM_MODE_TYPE_DRIVER        (1 << 6)

/* Backend types */
#define DRM_BACKEND_NONE    0
#define DRM_BACKEND_VIRTIO  1
#define DRM_BACKEND_BGA     2

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

/* ── KMS structures (Stage 1) ──────────────────────────────────── */

/* Display mode descriptor — matches Linux drm_mode_modeinfo layout */
typedef struct {
    uint32_t clock;             /* pixel clock in kHz */
    uint16_t hdisplay;
    uint16_t hsync_start;
    uint16_t hsync_end;
    uint16_t htotal;
    uint16_t vdisplay;
    uint16_t vsync_start;
    uint16_t vsync_end;
    uint16_t vtotal;
    uint16_t hskew;
    uint16_t vscan;
    uint32_t vrefresh;          /* vertical refresh rate in Hz */
    uint32_t flags;
    uint32_t type;              /* DRM_MODE_TYPE_* */
    char     name[DRM_DISPLAY_MODE_LEN];
} drm_mode_modeinfo_t;

/* DRM_IOCTL_MODE_GETRESOURCES */
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

/* DRM_IOCTL_MODE_GETCONNECTOR */
typedef struct {
    uint32_t            *encoders_ptr;
    drm_mode_modeinfo_t *modes_ptr;
    uint32_t            *props_ptr;
    uint64_t            *prop_values_ptr;
    uint32_t count_modes;
    uint32_t count_props;
    uint32_t count_encoders;
    uint32_t encoder_id;        /* current encoder */
    uint32_t connector_id;
    uint32_t connector_type;    /* DRM_MODE_CONNECTOR_* */
    uint32_t connector_type_id;
    uint32_t connection;        /* DRM_MODE_CONNECTED/DISCONNECTED/UNKNOWN */
    uint32_t mm_width;          /* physical size in mm */
    uint32_t mm_height;
    uint32_t subpixel;          /* DRM_MODE_SUBPIXEL_* */
    uint32_t pad;
} drm_mode_get_connector_t;

/* DRM_IOCTL_MODE_GETENCODER */
typedef struct {
    uint32_t encoder_id;
    uint32_t encoder_type;      /* DRM_MODE_ENCODER_* */
    uint32_t crtc_id;           /* currently bound CRTC */
    uint32_t possible_crtcs;    /* bitmask of usable CRTCs */
    uint32_t possible_clones;   /* bitmask of clone-capable encoders */
} drm_mode_get_encoder_t;

/* DRM_IOCTL_MODE_GETCRTC / DRM_IOCTL_MODE_SETCRTC */
typedef struct {
    uint32_t            *set_connectors_ptr;
    uint32_t             count_connectors;
    uint32_t             crtc_id;
    uint32_t             fb_id;     /* framebuffer id (0 = none, Stage 2) */
    uint32_t             x, y;      /* position in framebuffer */
    uint32_t             gamma_size;
    uint32_t             mode_valid; /* 1 if mode is set */
    drm_mode_modeinfo_t  mode;
} drm_mode_crtc_t;

/* ── DRM device state ───────────────────────────────────────────── */

typedef struct {
    int initialized;
    int backend;                /* DRM_BACKEND_* */

    /* CRTC state (single CRTC, id=1) */
    struct {
        uint32_t            id;
        uint32_t            fb_id;
        uint32_t            x, y;
        int                 mode_valid;
        drm_mode_modeinfo_t mode;
    } crtc;

    /* Encoder state (single encoder, id=1) */
    struct {
        uint32_t id;
        uint32_t type;
        uint32_t crtc_id;
    } encoder;

    /* Connector state (single connector, id=1) */
    struct {
        uint32_t            id;
        uint32_t            type;
        uint32_t            connection;
        uint32_t            encoder_id;
        uint32_t            mm_width;
        uint32_t            mm_height;
        int                 num_modes;
        drm_mode_modeinfo_t modes[DRM_MAX_MODES];
    } connector;

    /* Stage 2 will add: GEM handle table, framebuffer list */
} drm_device_t;

/* ── DRM API ────────────────────────────────────────────────────── */

/* Initialize the DRM subsystem (called once at boot) */
void drm_init(void);

/* Handle an ioctl on an open DRM fd. Returns 0 on success, -1 on error. */
int drm_ioctl(uint32_t cmd, void *arg);

/* Query if DRM is available */
int drm_is_available(void);

#endif
