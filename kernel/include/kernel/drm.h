#ifndef _KERNEL_DRM_H
#define _KERNEL_DRM_H

#include <stdint.h>
#include <kernel/ioctl.h>

/*
 * DRM (Direct Rendering Manager) subsystem for ImposOS.
 *
 * Stage 0: ioctl dispatch, DRM_IOCTL_VERSION, GET_CAP.
 * Stage 1: KMS modesetting — CRTC/connector/encoder abstractions.
 * Stage 2: GEM buffer management — dumb buffers, framebuffers, page flip.
 *
 * DRM ioctl type magic = 'd' (0x64), matching Linux.
 */

#define DRM_IOCTL_BASE  'd'

/* ── DRM ioctl command numbers ──────────────────────────────────── */

/* Core ioctls (Stage 0) */
#define DRM_IOCTL_VERSION           _IOWR(DRM_IOCTL_BASE, 0x00, sizeof(drm_version_t))
#define DRM_IOCTL_GEM_CLOSE         _IOW(DRM_IOCTL_BASE, 0x09, sizeof(drm_gem_close_t))
#define DRM_IOCTL_GET_CAP           _IOWR(DRM_IOCTL_BASE, 0x0C, sizeof(drm_get_cap_t))
#define DRM_IOCTL_SET_CLIENT_CAP    _IOW(DRM_IOCTL_BASE, 0x0D, sizeof(drm_set_client_cap_t))

/* KMS ioctls (Stage 1) */
#define DRM_IOCTL_MODE_GETRESOURCES _IOWR(DRM_IOCTL_BASE, 0xA0, sizeof(drm_mode_card_res_t))
#define DRM_IOCTL_MODE_GETCRTC      _IOWR(DRM_IOCTL_BASE, 0xA1, sizeof(drm_mode_crtc_t))
#define DRM_IOCTL_MODE_SETCRTC      _IOWR(DRM_IOCTL_BASE, 0xA2, sizeof(drm_mode_crtc_t))
#define DRM_IOCTL_MODE_GETENCODER   _IOWR(DRM_IOCTL_BASE, 0xA6, sizeof(drm_mode_get_encoder_t))
#define DRM_IOCTL_MODE_GETCONNECTOR _IOWR(DRM_IOCTL_BASE, 0xA7, sizeof(drm_mode_get_connector_t))

/* GEM / framebuffer ioctls (Stage 2) */
#define DRM_IOCTL_MODE_ADDFB        _IOWR(DRM_IOCTL_BASE, 0xAE, sizeof(drm_mode_fb_cmd_t))
#define DRM_IOCTL_MODE_RMFB         _IOWR(DRM_IOCTL_BASE, 0xAF, sizeof(uint32_t))
#define DRM_IOCTL_MODE_PAGE_FLIP    _IOWR(DRM_IOCTL_BASE, 0xB0, sizeof(drm_mode_page_flip_t))
#define DRM_IOCTL_MODE_CREATE_DUMB  _IOWR(DRM_IOCTL_BASE, 0xB2, sizeof(drm_mode_create_dumb_t))
#define DRM_IOCTL_MODE_MAP_DUMB     _IOWR(DRM_IOCTL_BASE, 0xB3, sizeof(drm_mode_map_dumb_t))
#define DRM_IOCTL_MODE_DESTROY_DUMB _IOWR(DRM_IOCTL_BASE, 0xB4, sizeof(drm_mode_destroy_dumb_t))

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

/* Page flip flags */
#define DRM_MODE_PAGE_FLIP_EVENT    0x01

/* Backend types */
#define DRM_BACKEND_NONE    0
#define DRM_BACKEND_VIRTIO  1
#define DRM_BACKEND_BGA     2

/* GEM / framebuffer limits */
#define DRM_GEM_MAX_OBJECTS     32
#define DRM_MAX_FRAMEBUFFERS    8

/* ── DRM structures ─────────────────────────────────────────────── */

/* DRM_IOCTL_VERSION */
typedef struct {
    int32_t  version_major;
    int32_t  version_minor;
    int32_t  version_patchlevel;
    uint32_t name_len;
    char    *name;
    uint32_t date_len;
    char    *date;
    uint32_t desc_len;
    char    *desc;
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

/* DRM_IOCTL_GEM_CLOSE */
typedef struct {
    uint32_t handle;
    uint32_t pad;
} drm_gem_close_t;

/* ── KMS structures (Stage 1) ──────────────────────────────────── */

/* Display mode descriptor */
typedef struct {
    uint32_t clock;
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
    uint32_t vrefresh;
    uint32_t flags;
    uint32_t type;
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
    uint32_t encoder_id;
    uint32_t connector_id;
    uint32_t connector_type;
    uint32_t connector_type_id;
    uint32_t connection;
    uint32_t mm_width;
    uint32_t mm_height;
    uint32_t subpixel;
    uint32_t pad;
} drm_mode_get_connector_t;

/* DRM_IOCTL_MODE_GETENCODER */
typedef struct {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} drm_mode_get_encoder_t;

/* DRM_IOCTL_MODE_GETCRTC / DRM_IOCTL_MODE_SETCRTC */
typedef struct {
    uint32_t            *set_connectors_ptr;
    uint32_t             count_connectors;
    uint32_t             crtc_id;
    uint32_t             fb_id;
    uint32_t             x, y;
    uint32_t             gamma_size;
    uint32_t             mode_valid;
    drm_mode_modeinfo_t  mode;
} drm_mode_crtc_t;

/* ── GEM / framebuffer structures (Stage 2) ────────────────────── */

/* DRM_IOCTL_MODE_CREATE_DUMB */
typedef struct {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    /* output */
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
} drm_mode_create_dumb_t;

/* DRM_IOCTL_MODE_MAP_DUMB */
typedef struct {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;    /* output: mmap offset (= physical address for identity-mapped kernel) */
} drm_mode_map_dumb_t;

/* DRM_IOCTL_MODE_DESTROY_DUMB */
typedef struct {
    uint32_t handle;
} drm_mode_destroy_dumb_t;

/* DRM_IOCTL_MODE_ADDFB */
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;    /* input: GEM handle */
    uint32_t fb_id;     /* output: framebuffer id */
} drm_mode_fb_cmd_t;

/* DRM_IOCTL_MODE_PAGE_FLIP */
typedef struct {
    uint32_t crtc_id;
    uint32_t fb_id;
    uint32_t flags;
    uint32_t reserved;
    uint64_t user_data;
} drm_mode_page_flip_t;

/* ── Internal GEM / framebuffer objects ────────────────────────── */

typedef struct {
    int      in_use;
    uint32_t handle;
    uint32_t phys_addr;     /* physical address of contiguous pages */
    uint32_t size;          /* size in bytes */
    uint32_t n_frames;      /* number of PMM frames */
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    int      refcount;
} drm_gem_object_t;

typedef struct {
    int      in_use;
    uint32_t fb_id;
    uint32_t gem_handle;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t phys_addr;     /* cached from GEM object */
} drm_framebuffer_t;

/* ── DRM device state ───────────────────────────────────────────── */

typedef struct {
    int initialized;
    int backend;

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

    /* GEM handle table (Stage 2) */
    uint32_t          next_gem_handle;
    drm_gem_object_t  gem_objects[DRM_GEM_MAX_OBJECTS];

    /* Framebuffer table (Stage 2) */
    uint32_t          next_fb_id;
    drm_framebuffer_t framebuffers[DRM_MAX_FRAMEBUFFERS];
} drm_device_t;

/* ── DRM API ────────────────────────────────────────────────────── */

void drm_init(void);
int drm_ioctl(uint32_t cmd, void *arg);
int drm_is_available(void);

#endif
