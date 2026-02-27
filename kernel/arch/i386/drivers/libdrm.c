/*
 * libdrm-compatible wrapper for ImposOS.
 *
 * Thin wrappers around drm_ioctl() that match the real Linux libdrm API.
 * Since the compositor runs in-kernel we skip the fd/syscall path and
 * call drm_ioctl() directly.  Returned objects are malloc'd — callers
 * must use the matching drmModeFree*() to avoid leaks.
 */

#include <kernel/libdrm.h>
#include <kernel/drm.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Pseudo-fd returned by drmOpen().  We only have one DRM device. */
#define DRM_PSEUDO_FD  100

/* ── Core API ──────────────────────────────────────────────────────── */

int drmOpen(const char *name, const char *busid) {
    (void)name;
    (void)busid;
    if (!drm_is_available())
        return -1;
    return DRM_PSEUDO_FD;
}

int drmClose(int fd) {
    (void)fd;
    return 0;
}

int drmIoctl(int fd, unsigned long request, void *arg) {
    (void)fd;
    return drm_ioctl((uint32_t)request, arg);
}

drmVersionPtr drmGetVersion(int fd) {
    static char name_buf[64];
    static char date_buf[64];
    static char desc_buf[128];

    drm_version_t ver;
    memset(&ver, 0, sizeof(ver));
    ver.name     = name_buf;
    ver.name_len = sizeof(name_buf) - 1;
    ver.date     = date_buf;
    ver.date_len = sizeof(date_buf) - 1;
    ver.desc     = desc_buf;
    ver.desc_len = sizeof(desc_buf) - 1;

    if (drmIoctl(fd, DRM_IOCTL_VERSION, &ver) != 0)
        return NULL;

    drmVersionPtr v = malloc(sizeof(drmVersion));
    if (!v) return NULL;

    v->version_major      = ver.version_major;
    v->version_minor      = ver.version_minor;
    v->version_patchlevel = ver.version_patchlevel;

    v->name_len = ver.name_len;
    v->name     = malloc(ver.name_len + 1);
    if (v->name) {
        memcpy(v->name, name_buf, ver.name_len);
        v->name[ver.name_len] = '\0';
    }

    v->date_len = ver.date_len;
    v->date     = malloc(ver.date_len + 1);
    if (v->date) {
        memcpy(v->date, date_buf, ver.date_len);
        v->date[ver.date_len] = '\0';
    }

    v->desc_len = ver.desc_len;
    v->desc     = malloc(ver.desc_len + 1);
    if (v->desc) {
        memcpy(v->desc, desc_buf, ver.desc_len);
        v->desc[ver.desc_len] = '\0';
    }

    return v;
}

void drmFreeVersion(drmVersionPtr v) {
    if (!v) return;
    free(v->name);
    free(v->date);
    free(v->desc);
    free(v);
}

int drmGetCap(int fd, uint64_t capability, uint64_t *value) {
    drm_get_cap_t cap;
    cap.capability = capability;
    cap.value      = 0;
    int ret = drmIoctl(fd, DRM_IOCTL_GET_CAP, &cap);
    if (ret == 0 && value)
        *value = cap.value;
    return ret;
}

/* ── Mode-setting: Resources ──────────────────────────────────────── */

drmModeResPtr drmModeGetResources(int fd) {
    drm_mode_card_res_t res;
    memset(&res, 0, sizeof(res));

    /* First call: get counts */
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0)
        return NULL;

    drmModeResPtr r = calloc(1, sizeof(drmModeRes));
    if (!r) return NULL;

    r->count_fbs        = res.count_fbs;
    r->count_crtcs      = res.count_crtcs;
    r->count_connectors = res.count_connectors;
    r->count_encoders   = res.count_encoders;
    r->min_width        = res.min_width;
    r->max_width        = res.max_width;
    r->min_height       = res.min_height;
    r->max_height       = res.max_height;

    /* Allocate arrays for the second call */
    if (r->count_fbs)
        r->fbs = calloc(r->count_fbs, sizeof(uint32_t));
    if (r->count_crtcs)
        r->crtcs = calloc(r->count_crtcs, sizeof(uint32_t));
    if (r->count_connectors)
        r->connectors = calloc(r->count_connectors, sizeof(uint32_t));
    if (r->count_encoders)
        r->encoders = calloc(r->count_encoders, sizeof(uint32_t));

    /* Second call: fill arrays */
    res.fb_id_ptr        = r->fbs;
    res.crtc_id_ptr      = r->crtcs;
    res.connector_id_ptr = r->connectors;
    res.encoder_id_ptr   = r->encoders;

    if (drmIoctl(fd, DRM_IOCTL_MODE_GETRESOURCES, &res) != 0) {
        drmModeFreeResources(r);
        return NULL;
    }

    return r;
}

void drmModeFreeResources(drmModeResPtr res) {
    if (!res) return;
    free(res->fbs);
    free(res->crtcs);
    free(res->connectors);
    free(res->encoders);
    free(res);
}

/* ── Mode-setting: Connectors ─────────────────────────────────────── */

drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t connector_id) {
    drm_mode_get_connector_t conn;
    memset(&conn, 0, sizeof(conn));
    conn.connector_id = connector_id;

    /* First call: get counts */
    if (drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0)
        return NULL;

    drmModeConnectorPtr c = calloc(1, sizeof(drmModeConnector));
    if (!c) return NULL;

    c->connector_id      = conn.connector_id;
    c->encoder_id        = conn.encoder_id;
    c->connector_type    = conn.connector_type;
    c->connector_type_id = conn.connector_type_id;
    c->connection        = conn.connection;
    c->mm_width          = conn.mm_width;
    c->mm_height         = conn.mm_height;
    c->subpixel          = conn.subpixel;
    c->count_modes       = conn.count_modes;
    c->count_props       = conn.count_props;
    c->count_encoders    = conn.count_encoders;

    /* Allocate arrays for second call */
    if (c->count_modes)
        c->modes = calloc(c->count_modes, sizeof(drmModeModeInfo));
    if (c->count_props) {
        c->props      = calloc(c->count_props, sizeof(uint32_t));
        c->prop_values = calloc(c->count_props, sizeof(uint64_t));
    }
    if (c->count_encoders)
        c->encoders = calloc(c->count_encoders, sizeof(uint32_t));

    /* Second call: fill arrays */
    conn.modes_ptr       = (drm_mode_modeinfo_t *)c->modes;
    conn.props_ptr       = c->props;
    conn.prop_values_ptr = c->prop_values;
    conn.encoders_ptr    = c->encoders;

    if (drmIoctl(fd, DRM_IOCTL_MODE_GETCONNECTOR, &conn) != 0) {
        drmModeFreeConnector(c);
        return NULL;
    }

    return c;
}

void drmModeFreeConnector(drmModeConnectorPtr conn) {
    if (!conn) return;
    free(conn->modes);
    free(conn->props);
    free(conn->prop_values);
    free(conn->encoders);
    free(conn);
}

/* ── Mode-setting: Encoders ───────────────────────────────────────── */

drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t encoder_id) {
    drm_mode_get_encoder_t enc;
    memset(&enc, 0, sizeof(enc));
    enc.encoder_id = encoder_id;

    if (drmIoctl(fd, DRM_IOCTL_MODE_GETENCODER, &enc) != 0)
        return NULL;

    drmModeEncoderPtr e = malloc(sizeof(drmModeEncoder));
    if (!e) return NULL;

    e->encoder_id      = enc.encoder_id;
    e->encoder_type    = enc.encoder_type;
    e->crtc_id         = enc.crtc_id;
    e->possible_crtcs  = enc.possible_crtcs;
    e->possible_clones = enc.possible_clones;

    return e;
}

void drmModeFreeEncoder(drmModeEncoderPtr enc) {
    free(enc);
}

/* ── Mode-setting: CRTCs ──────────────────────────────────────────── */

drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t crtc_id) {
    drm_mode_crtc_t crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id = crtc_id;

    if (drmIoctl(fd, DRM_IOCTL_MODE_GETCRTC, &crtc) != 0)
        return NULL;

    drmModeCrtcPtr c = calloc(1, sizeof(drmModeCrtc));
    if (!c) return NULL;

    c->crtc_id    = crtc.crtc_id;
    c->buffer_id  = crtc.fb_id;
    c->x          = crtc.x;
    c->y          = crtc.y;
    c->mode_valid = crtc.mode_valid;
    c->gamma_size = crtc.gamma_size;

    if (crtc.mode_valid) {
        c->width  = crtc.mode.hdisplay;
        c->height = crtc.mode.vdisplay;
        memcpy(&c->mode, &crtc.mode, sizeof(drmModeModeInfo));
    }

    return c;
}

void drmModeFreeCrtc(drmModeCrtcPtr crtc) {
    free(crtc);
}

int drmModeSetCrtc(int fd, uint32_t crtc_id, uint32_t fb_id,
                   uint32_t x, uint32_t y,
                   uint32_t *connectors, int count,
                   drmModeModeInfoPtr mode) {
    drm_mode_crtc_t crtc;
    memset(&crtc, 0, sizeof(crtc));
    crtc.crtc_id            = crtc_id;
    crtc.fb_id              = fb_id;
    crtc.x                  = x;
    crtc.y                  = y;
    crtc.set_connectors_ptr = connectors;
    crtc.count_connectors   = (uint32_t)count;

    if (mode) {
        crtc.mode_valid = 1;
        memcpy(&crtc.mode, mode, sizeof(drm_mode_modeinfo_t));
    }

    return drmIoctl(fd, DRM_IOCTL_MODE_SETCRTC, &crtc);
}

/* ── Framebuffers ──────────────────────────────────────────────────── */

int drmModeAddFB(int fd, uint32_t width, uint32_t height,
                 uint8_t depth, uint8_t bpp,
                 uint32_t pitch, uint32_t bo_handle,
                 uint32_t *buf_id) {
    drm_mode_fb_cmd_t fb;
    memset(&fb, 0, sizeof(fb));
    fb.width  = width;
    fb.height = height;
    fb.pitch  = pitch;
    fb.bpp    = bpp;
    fb.depth  = depth;
    fb.handle = bo_handle;

    int ret = drmIoctl(fd, DRM_IOCTL_MODE_ADDFB, &fb);
    if (ret == 0 && buf_id)
        *buf_id = fb.fb_id;
    return ret;
}

int drmModeRmFB(int fd, uint32_t fb_id) {
    return drmIoctl(fd, DRM_IOCTL_MODE_RMFB, &fb_id);
}

/* ── Page flip ─────────────────────────────────────────────────────── */

int drmModePageFlip(int fd, uint32_t crtc_id, uint32_t fb_id,
                    uint32_t flags, void *user_data) {
    drm_mode_page_flip_t flip;
    memset(&flip, 0, sizeof(flip));
    flip.crtc_id   = crtc_id;
    flip.fb_id     = fb_id;
    flip.flags     = flags;
    flip.user_data = (uint64_t)(uintptr_t)user_data;

    return drmIoctl(fd, DRM_IOCTL_MODE_PAGE_FLIP, &flip);
}

/* ── Dumb buffer management ────────────────────────────────────────── */

int drmModeCreateDumbBuffer(int fd, uint32_t width, uint32_t height,
                            uint32_t bpp, uint32_t flags,
                            uint32_t *handle, uint32_t *pitch,
                            uint64_t *size) {
    drm_mode_create_dumb_t req;
    memset(&req, 0, sizeof(req));
    req.width  = width;
    req.height = height;
    req.bpp    = bpp;
    req.flags  = flags;

    int ret = drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &req);
    if (ret == 0) {
        if (handle) *handle = req.handle;
        if (pitch)  *pitch  = req.pitch;
        if (size)   *size   = req.size;
    }
    return ret;
}

int drmModeMapDumbBuffer(int fd, uint32_t handle, uint64_t *offset) {
    drm_mode_map_dumb_t map;
    memset(&map, 0, sizeof(map));
    map.handle = handle;

    int ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map);
    if (ret == 0 && offset)
        *offset = map.offset;
    return ret;
}

int drmModeDestroyDumbBuffer(int fd, uint32_t handle) {
    drm_mode_destroy_dumb_t req;
    req.handle = handle;
    return drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &req);
}

int drmCloseBufferHandle(int fd, uint32_t handle) {
    drm_gem_close_t req;
    req.handle = handle;
    req.pad    = 0;
    return drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &req);
}
