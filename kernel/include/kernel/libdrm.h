#ifndef _KERNEL_LIBDRM_H
#define _KERNEL_LIBDRM_H

/*
 * libdrm-compatible API for ImposOS.
 *
 * Provides the same function signatures and struct layouts as Linux libdrm
 * (xf86drm.h / xf86drmMode.h), but implemented as thin wrappers around
 * our kernel drm_ioctl().  Since the compositor runs in-kernel, these
 * bypass the fd/syscall layer entirely.
 *
 * Naming convention matches upstream libdrm exactly so that porting
 * DRM-based code is copy-paste straightforward.
 */

#include <stdint.h>

/* ── Types matching xf86drm.h ─────────────────────────────────────── */

typedef struct _drmVersion {
    int    version_major;
    int    version_minor;
    int    version_patchlevel;
    int    name_len;
    char  *name;
    int    date_len;
    char  *date;
    int    desc_len;
    char  *desc;
} drmVersion, *drmVersionPtr;

/* ── Types matching xf86drmMode.h ─────────────────────────────────── */

typedef struct _drmModeModeInfo {
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
    char     name[32];
} drmModeModeInfo, *drmModeModeInfoPtr;

typedef struct _drmModeRes {
    int       count_fbs;
    uint32_t *fbs;
    int       count_crtcs;
    uint32_t *crtcs;
    int       count_connectors;
    uint32_t *connectors;
    int       count_encoders;
    uint32_t *encoders;
    uint32_t  min_width, max_width;
    uint32_t  min_height, max_height;
} drmModeRes, *drmModeResPtr;

typedef struct _drmModeConnector {
    uint32_t          connector_id;
    uint32_t          encoder_id;
    uint32_t          connector_type;
    uint32_t          connector_type_id;
    uint32_t          connection;       /* DRM_MODE_CONNECTED, etc. */
    uint32_t          mm_width;
    uint32_t          mm_height;
    uint32_t          subpixel;
    int               count_modes;
    drmModeModeInfo  *modes;
    int               count_props;
    uint32_t         *props;
    uint64_t         *prop_values;
    int               count_encoders;
    uint32_t         *encoders;
} drmModeConnector, *drmModeConnectorPtr;

typedef struct _drmModeEncoder {
    uint32_t encoder_id;
    uint32_t encoder_type;
    uint32_t crtc_id;
    uint32_t possible_crtcs;
    uint32_t possible_clones;
} drmModeEncoder, *drmModeEncoderPtr;

typedef struct _drmModeCrtc {
    uint32_t         crtc_id;
    uint32_t         buffer_id;     /* current fb_id */
    uint32_t         x, y;
    uint32_t         width, height;
    int              mode_valid;
    drmModeModeInfo  mode;
    int              gamma_size;
} drmModeCrtc, *drmModeCrtcPtr;

typedef struct _drmModeFB {
    uint32_t fb_id;
    uint32_t width, height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t depth;
    uint32_t handle;
} drmModeFB, *drmModeFBPtr;

/* ── Dumb buffer create/map/destroy structs ────────────────────────── */

typedef struct _drmModeCreateDumb {
    uint32_t height;
    uint32_t width;
    uint32_t bpp;
    uint32_t flags;
    /* output */
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
} drmModeCreateDumb;

typedef struct _drmModeMapDumb {
    uint32_t handle;
    uint32_t pad;
    uint64_t offset;
} drmModeMapDumb;

typedef struct _drmModeDestroyDumb {
    uint32_t handle;
} drmModeDestroyDumb;

/* ── Core API (xf86drm.h equivalents) ─────────────────────────────── */

/* Open/close the DRM device.  Returns a pseudo-fd (always 100). */
int             drmOpen(const char *name, const char *busid);
int             drmClose(int fd);

/* Version query */
drmVersionPtr   drmGetVersion(int fd);
void            drmFreeVersion(drmVersionPtr v);

/* Capability query */
int             drmGetCap(int fd, uint64_t capability, uint64_t *value);

/* Raw ioctl passthrough */
int             drmIoctl(int fd, unsigned long request, void *arg);

/* ── Mode-setting API (xf86drmMode.h equivalents) ─────────────────── */

/* Resources */
drmModeResPtr       drmModeGetResources(int fd);
void                drmModeFreeResources(drmModeResPtr res);

/* Connectors */
drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id);
void                drmModeFreeConnector(drmModeConnectorPtr conn);

/* Encoders */
drmModeEncoderPtr   drmModeGetEncoder(int fd, uint32_t encoder_id);
void                drmModeFreeEncoder(drmModeEncoderPtr enc);

/* CRTCs */
drmModeCrtcPtr      drmModeGetCrtc(int fd, uint32_t crtc_id);
void                drmModeFreeCrtc(drmModeCrtcPtr crtc);
int                 drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t fb_id,
                                   uint32_t x, uint32_t y,
                                   uint32_t *connectors, int count,
                                   drmModeModeInfoPtr mode);

/* Framebuffers */
int                 drmModeAddFB(int fd, uint32_t width, uint32_t height,
                                 uint8_t depth, uint8_t bpp,
                                 uint32_t pitch, uint32_t bo_handle,
                                 uint32_t *buf_id);
int                 drmModeRmFB(int fd, uint32_t fb_id);

/* Page flip */
int                 drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                                    uint32_t flags, void *user_data);

/* Dumb buffer management */
int                 drmModeCreateDumbBuffer(int fd, uint32_t width,
                                            uint32_t height, uint32_t bpp,
                                            uint32_t flags, uint32_t *handle,
                                            uint32_t *pitch, uint64_t *size);
int                 drmModeMapDumbBuffer(int fd, uint32_t handle,
                                         uint64_t *offset);
int                 drmModeDestroyDumbBuffer(int fd, uint32_t handle);

/* GEM close */
int                 drmCloseBufferHandle(int fd, uint32_t handle);

#endif /* _KERNEL_LIBDRM_H */
