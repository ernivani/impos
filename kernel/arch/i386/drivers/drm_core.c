#include <kernel/drm.h>
#include <kernel/ioctl.h>
#include <kernel/io.h>
#include <kernel/gfx.h>
#include <kernel/virtio_gpu.h>
#include <kernel/pmm.h>
#include <string.h>
#include <stdio.h>

/*
 * DRM Core — Stages 0-2: ioctl dispatch, KMS modesetting, GEM buffers.
 *
 * Stage 0: VERSION, GET_CAP, SET_CLIENT_CAP
 * Stage 1: KMS — GETRESOURCES, GETCONNECTOR, GETENCODER, GETCRTC, SETCRTC
 * Stage 2: GEM — CREATE_DUMB, MAP_DUMB, DESTROY_DUMB, GEM_CLOSE,
 *                 ADDFB, RMFB, PAGE_FLIP
 * Stage 4: DRM-backed compositor — zero-copy flip when GEM == backbuffer
 */

/* ── Driver identity ────────────────────────────────────────────── */

#define DRM_DRIVER_NAME     "impos-gpu"
#define DRM_DRIVER_DATE     "20260227"
#define DRM_DRIVER_DESC     "ImposOS DRM driver"
#define DRM_VERSION_MAJOR   0
#define DRM_VERSION_MINOR   4
#define DRM_VERSION_PATCH   0

#define PAGE_SIZE 4096

/* ── Global DRM device state ────────────────────────────────────── */

static drm_device_t drm_dev;

/* ── Mode helpers ──────────────────────────────────────────────── */

static void drm_build_mode(drm_mode_modeinfo_t *m, uint32_t w, uint32_t h,
                            uint32_t refresh, uint32_t type_flags) {
    memset(m, 0, sizeof(*m));
    m->hdisplay = (uint16_t)w;
    m->vdisplay = (uint16_t)h;
    m->vrefresh = refresh;
    m->type = DRM_MODE_TYPE_DRIVER | type_flags;

    m->hsync_start = (uint16_t)(w + 48);
    m->hsync_end   = (uint16_t)(w + 48 + 112);
    m->htotal      = (uint16_t)(w + 48 + 112 + 80);
    m->vsync_start = (uint16_t)(h + 3);
    m->vsync_end   = (uint16_t)(h + 3 + 6);
    m->vtotal      = (uint16_t)(h + 3 + 6 + 25);
    m->clock       = (uint32_t)m->htotal * m->vtotal * refresh / 1000;

    snprintf(m->name, DRM_DISPLAY_MODE_LEN, "%ux%u", w, h);
}

static int drm_add_mode(uint32_t w, uint32_t h, uint32_t refresh,
                         uint32_t type_flags) {
    if (drm_dev.connector.num_modes >= DRM_MAX_MODES)
        return -1;

    for (int i = 0; i < drm_dev.connector.num_modes; i++) {
        if (drm_dev.connector.modes[i].hdisplay == (uint16_t)w &&
            drm_dev.connector.modes[i].vdisplay == (uint16_t)h)
            return 0;
    }

    drm_build_mode(&drm_dev.connector.modes[drm_dev.connector.num_modes],
                    w, h, refresh, type_flags);
    drm_dev.connector.num_modes++;
    return 0;
}

/* ── GEM helpers ───────────────────────────────────────────────── */

static drm_gem_object_t *gem_find_by_handle(uint32_t handle) {
    for (int i = 0; i < DRM_GEM_MAX_OBJECTS; i++) {
        if (drm_dev.gem_objects[i].in_use &&
            drm_dev.gem_objects[i].handle == handle)
            return &drm_dev.gem_objects[i];
    }
    return NULL;
}

static drm_gem_object_t *gem_alloc_slot(void) {
    for (int i = 0; i < DRM_GEM_MAX_OBJECTS; i++) {
        if (!drm_dev.gem_objects[i].in_use)
            return &drm_dev.gem_objects[i];
    }
    return NULL;
}

static drm_framebuffer_t *fb_find_by_id(uint32_t fb_id) {
    for (int i = 0; i < DRM_MAX_FRAMEBUFFERS; i++) {
        if (drm_dev.framebuffers[i].in_use &&
            drm_dev.framebuffers[i].fb_id == fb_id)
            return &drm_dev.framebuffers[i];
    }
    return NULL;
}

static drm_framebuffer_t *fb_alloc_slot(void) {
    for (int i = 0; i < DRM_MAX_FRAMEBUFFERS; i++) {
        if (!drm_dev.framebuffers[i].in_use)
            return &drm_dev.framebuffers[i];
    }
    return NULL;
}

/* Flip a framebuffer to the display.
 * If the GEM buffer IS the backbuffer (compositor DRM integration),
 * skip the copy — the compositor already rendered into it.
 * Otherwise, copy GEM → backbuffer row by row. */
static void drm_flip_fb(drm_framebuffer_t *fb) {
    uint32_t *backbuf = gfx_backbuffer();
    uint32_t disp_w = gfx_width();
    uint32_t disp_h = gfx_height();
    uint32_t disp_pitch = gfx_pitch();  /* bytes */

    if (!backbuf || !fb->phys_addr) return;

    uint32_t *src = (uint32_t *)fb->phys_addr;
    uint32_t copy_w = fb->width < disp_w ? fb->width : disp_w;
    uint32_t copy_h = fb->height < disp_h ? fb->height : disp_h;

    /* Zero-copy path: GEM buffer is already the backbuffer */
    if (src != backbuf) {
        for (uint32_t y = 0; y < copy_h; y++) {
            uint32_t *dst_row = (uint32_t *)((uint8_t *)backbuf + y * disp_pitch);
            uint32_t *src_row = (uint32_t *)((uint8_t *)src + y * fb->pitch);
            memcpy(dst_row, src_row, copy_w * 4);
        }
    }

    /* Trigger display update */
    if (gfx_using_virtio_gpu()) {
        virtio_gpu_transfer_2d(0, 0, (int)copy_w, (int)copy_h);
        virtio_gpu_flush(0, 0, (int)copy_w, (int)copy_h);
    } else {
        gfx_flip_rect(0, 0, (int)copy_w, (int)copy_h);
    }
}

/* ── Stage 0 ioctl handlers ────────────────────────────────────── */

static int drm_ioctl_version(drm_version_t *ver) {
    if (!ver)
        return -1;

    ver->version_major = DRM_VERSION_MAJOR;
    ver->version_minor = DRM_VERSION_MINOR;
    ver->version_patchlevel = DRM_VERSION_PATCH;

    uint32_t name_len = sizeof(DRM_DRIVER_NAME) - 1;
    if (ver->name && ver->name_len > 0) {
        uint32_t copy_len = ver->name_len < name_len ? ver->name_len : name_len;
        memcpy(ver->name, DRM_DRIVER_NAME, copy_len);
        if (copy_len < ver->name_len)
            ver->name[copy_len] = '\0';
    }
    ver->name_len = name_len;

    uint32_t date_len = sizeof(DRM_DRIVER_DATE) - 1;
    if (ver->date && ver->date_len > 0) {
        uint32_t copy_len = ver->date_len < date_len ? ver->date_len : date_len;
        memcpy(ver->date, DRM_DRIVER_DATE, copy_len);
        if (copy_len < ver->date_len)
            ver->date[copy_len] = '\0';
    }
    ver->date_len = date_len;

    uint32_t desc_len = sizeof(DRM_DRIVER_DESC) - 1;
    if (ver->desc && ver->desc_len > 0) {
        uint32_t copy_len = ver->desc_len < desc_len ? ver->desc_len : desc_len;
        memcpy(ver->desc, DRM_DRIVER_DESC, copy_len);
        if (copy_len < ver->desc_len)
            ver->desc[copy_len] = '\0';
    }
    ver->desc_len = desc_len;

    return 0;
}

static int drm_ioctl_get_cap(drm_get_cap_t *cap) {
    if (!cap)
        return -1;

    switch (cap->capability) {
        case DRM_CAP_DUMB_BUFFER:
            cap->value = 1;
            return 0;
        case DRM_CAP_PRIME:
            cap->value = 0;
            return 0;
        case DRM_CAP_TIMESTAMP_MONOTONIC:
            cap->value = 1;
            return 0;
        default:
            cap->value = 0;
            return 0;
    }
}

static int drm_ioctl_set_client_cap(drm_set_client_cap_t *cap) {
    if (!cap)
        return -1;

    switch (cap->capability) {
        case DRM_CLIENT_CAP_UNIVERSAL_PLANES:
        case DRM_CLIENT_CAP_ATOMIC:
            return 0;
        default:
            return -1;
    }
}

/* ── Stage 1 KMS ioctl handlers ────────────────────────────────── */

static int drm_ioctl_mode_getresources(drm_mode_card_res_t *res) {
    if (!res)
        return -1;

    uint32_t crtc_id = drm_dev.crtc.id;
    uint32_t conn_id = drm_dev.connector.id;
    uint32_t enc_id  = drm_dev.encoder.id;

    if (res->crtc_id_ptr && res->count_crtcs >= 1)
        res->crtc_id_ptr[0] = crtc_id;
    if (res->connector_id_ptr && res->count_connectors >= 1)
        res->connector_id_ptr[0] = conn_id;
    if (res->encoder_id_ptr && res->count_encoders >= 1)
        res->encoder_id_ptr[0] = enc_id;

    /* Count active framebuffers */
    uint32_t fb_count = 0;
    for (int i = 0; i < DRM_MAX_FRAMEBUFFERS; i++) {
        if (drm_dev.framebuffers[i].in_use) {
            if (res->fb_id_ptr && fb_count < res->count_fbs)
                res->fb_id_ptr[fb_count] = drm_dev.framebuffers[i].fb_id;
            fb_count++;
        }
    }

    res->count_fbs = fb_count;
    res->count_crtcs = 1;
    res->count_connectors = 1;
    res->count_encoders = 1;

    res->min_width  = 640;
    res->max_width  = 1920;
    res->min_height = 480;
    res->max_height = 1080;

    return 0;
}

static int drm_ioctl_mode_getconnector(drm_mode_get_connector_t *conn) {
    if (!conn)
        return -1;

    if (conn->connector_id != drm_dev.connector.id)
        return -1;

    uint32_t num = (uint32_t)drm_dev.connector.num_modes;
    if (conn->modes_ptr && conn->count_modes > 0) {
        uint32_t copy = conn->count_modes < num ? conn->count_modes : num;
        memcpy(conn->modes_ptr, drm_dev.connector.modes,
               copy * sizeof(drm_mode_modeinfo_t));
    }

    if (conn->encoders_ptr && conn->count_encoders >= 1)
        conn->encoders_ptr[0] = drm_dev.encoder.id;

    conn->count_modes = num;
    conn->count_props = 0;
    conn->count_encoders = 1;
    conn->encoder_id = drm_dev.connector.encoder_id;
    conn->connector_type = drm_dev.connector.type;
    conn->connector_type_id = 1;
    conn->connection = drm_dev.connector.connection;
    conn->mm_width = drm_dev.connector.mm_width;
    conn->mm_height = drm_dev.connector.mm_height;
    conn->subpixel = DRM_MODE_SUBPIXEL_UNKNOWN;

    return 0;
}

static int drm_ioctl_mode_getencoder(drm_mode_get_encoder_t *enc) {
    if (!enc)
        return -1;

    if (enc->encoder_id != drm_dev.encoder.id)
        return -1;

    enc->encoder_type = drm_dev.encoder.type;
    enc->crtc_id = drm_dev.encoder.crtc_id;
    enc->possible_crtcs = 1;
    enc->possible_clones = 0;

    return 0;
}

static int drm_ioctl_mode_getcrtc(drm_mode_crtc_t *crtc) {
    if (!crtc)
        return -1;

    if (crtc->crtc_id != drm_dev.crtc.id)
        return -1;

    crtc->fb_id = drm_dev.crtc.fb_id;
    crtc->x = drm_dev.crtc.x;
    crtc->y = drm_dev.crtc.y;
    crtc->gamma_size = 0;
    crtc->mode_valid = drm_dev.crtc.mode_valid;
    if (drm_dev.crtc.mode_valid)
        memcpy(&crtc->mode, &drm_dev.crtc.mode, sizeof(drm_mode_modeinfo_t));

    return 0;
}

static int drm_ioctl_mode_setcrtc(drm_mode_crtc_t *crtc) {
    if (!crtc)
        return -1;

    if (crtc->crtc_id != drm_dev.crtc.id)
        return -1;

    if (!crtc->mode_valid) {
        drm_dev.crtc.mode_valid = 0;
        drm_dev.crtc.fb_id = 0;
        DBG("DRM: CRTC disabled");
        return 0;
    }

    uint16_t req_w = crtc->mode.hdisplay;
    uint16_t req_h = crtc->mode.vdisplay;
    int found = 0;
    for (int i = 0; i < drm_dev.connector.num_modes; i++) {
        if (drm_dev.connector.modes[i].hdisplay == req_w &&
            drm_dev.connector.modes[i].vdisplay == req_h) {
            found = 1;
            break;
        }
    }
    if (!found) {
        DBG("DRM: SETCRTC rejected — mode %ux%u not in mode list", req_w, req_h);
        return -1;
    }

    memcpy(&drm_dev.crtc.mode, &crtc->mode, sizeof(drm_mode_modeinfo_t));
    drm_dev.crtc.mode_valid = 1;
    drm_dev.crtc.fb_id = crtc->fb_id;
    drm_dev.crtc.x = crtc->x;
    drm_dev.crtc.y = crtc->y;

    if (drm_dev.backend == DRM_BACKEND_BGA) {
        bga_set_mode((int)req_w, (int)req_h, 32);
        DBG("DRM: BGA mode set to %ux%u", req_w, req_h);
    } else {
        DBG("DRM: Mode recorded %ux%u", req_w, req_h);
    }

    /* If a framebuffer is attached, display it */
    if (crtc->fb_id) {
        drm_framebuffer_t *fb = fb_find_by_id(crtc->fb_id);
        if (fb) drm_flip_fb(fb);
    }

    return 0;
}

/* ── Stage 2 GEM ioctl handlers ────────────────────────────────── */

static int drm_ioctl_mode_create_dumb(drm_mode_create_dumb_t *args) {
    if (!args || args->width == 0 || args->height == 0 || args->bpp == 0)
        return -1;

    /* Calculate buffer dimensions */
    uint32_t pitch = args->width * (args->bpp / 8);
    /* Align pitch to 64 bytes for cache-line alignment */
    pitch = (pitch + 63) & ~63u;
    uint64_t size = (uint64_t)pitch * args->height;
    if (size > 16 * 1024 * 1024)  /* 16MB max per buffer */
        return -1;

    uint32_t n_frames = ((uint32_t)size + PAGE_SIZE - 1) / PAGE_SIZE;

    /* Allocate contiguous physical frames */
    uint32_t phys = pmm_alloc_contiguous(n_frames);
    if (!phys) {
        DBG("DRM: CREATE_DUMB failed — can't alloc %u contiguous frames", n_frames);
        return -1;
    }

    /* Zero the buffer */
    memset((void *)phys, 0, n_frames * PAGE_SIZE);

    /* Find a free GEM slot */
    drm_gem_object_t *gem = gem_alloc_slot();
    if (!gem) {
        pmm_free_contiguous(phys, n_frames);
        return -1;
    }

    gem->in_use = 1;
    gem->handle = drm_dev.next_gem_handle++;
    gem->phys_addr = phys;
    gem->size = (uint32_t)size;
    gem->n_frames = n_frames;
    gem->width = args->width;
    gem->height = args->height;
    gem->pitch = pitch;
    gem->bpp = args->bpp;
    gem->refcount = 1;

    /* Return to caller */
    args->handle = gem->handle;
    args->pitch = pitch;
    args->size = size;

    DBG("DRM: CREATE_DUMB handle=%u %ux%u bpp=%u pitch=%u phys=0x%x (%u frames)",
        gem->handle, args->width, args->height, args->bpp, pitch, phys, n_frames);
    return 0;
}

static int drm_ioctl_mode_map_dumb(drm_mode_map_dumb_t *args) {
    if (!args)
        return -1;

    drm_gem_object_t *gem = gem_find_by_handle(args->handle);
    if (!gem)
        return -1;

    /* In our identity-mapped kernel, the offset IS the physical address.
     * Userspace (or kernel code) can directly write to this address. */
    args->offset = (uint64_t)gem->phys_addr;
    return 0;
}

static int drm_ioctl_mode_destroy_dumb(drm_mode_destroy_dumb_t *args) {
    if (!args)
        return -1;

    drm_gem_object_t *gem = gem_find_by_handle(args->handle);
    if (!gem)
        return -1;

    gem->refcount--;
    if (gem->refcount <= 0) {
        pmm_free_contiguous(gem->phys_addr, gem->n_frames);
        DBG("DRM: DESTROY_DUMB handle=%u freed %u frames at 0x%x",
            gem->handle, gem->n_frames, gem->phys_addr);
        memset(gem, 0, sizeof(*gem));
    }
    return 0;
}

static int drm_ioctl_gem_close(drm_gem_close_t *args) {
    if (!args)
        return -1;

    drm_gem_object_t *gem = gem_find_by_handle(args->handle);
    if (!gem)
        return -1;

    gem->refcount--;
    if (gem->refcount <= 0) {
        pmm_free_contiguous(gem->phys_addr, gem->n_frames);
        memset(gem, 0, sizeof(*gem));
    }
    return 0;
}

static int drm_ioctl_mode_addfb(drm_mode_fb_cmd_t *args) {
    if (!args)
        return -1;

    /* Validate the GEM handle */
    drm_gem_object_t *gem = gem_find_by_handle(args->handle);
    if (!gem)
        return -1;

    /* Validate dimensions fit in the GEM buffer */
    uint64_t needed = (uint64_t)args->pitch * args->height;
    if (needed > gem->size)
        return -1;

    /* Find a free framebuffer slot */
    drm_framebuffer_t *fb = fb_alloc_slot();
    if (!fb)
        return -1;

    fb->in_use = 1;
    fb->fb_id = drm_dev.next_fb_id++;
    fb->gem_handle = args->handle;
    fb->width = args->width;
    fb->height = args->height;
    fb->pitch = args->pitch;
    fb->bpp = args->bpp;
    fb->depth = args->depth;
    fb->phys_addr = gem->phys_addr;

    /* Bump GEM refcount since FB holds a reference */
    gem->refcount++;

    args->fb_id = fb->fb_id;

    DBG("DRM: ADDFB fb_id=%u gem=%u %ux%u pitch=%u",
        fb->fb_id, args->handle, args->width, args->height, args->pitch);
    return 0;
}

static int drm_ioctl_mode_rmfb(uint32_t *fb_id_ptr) {
    if (!fb_id_ptr)
        return -1;

    uint32_t fb_id = *fb_id_ptr;
    drm_framebuffer_t *fb = fb_find_by_id(fb_id);
    if (!fb)
        return -1;

    /* If this fb is currently displayed, detach it */
    if (drm_dev.crtc.fb_id == fb_id)
        drm_dev.crtc.fb_id = 0;

    /* Release GEM reference */
    drm_gem_object_t *gem = gem_find_by_handle(fb->gem_handle);
    if (gem) {
        gem->refcount--;
        if (gem->refcount <= 0) {
            pmm_free_contiguous(gem->phys_addr, gem->n_frames);
            memset(gem, 0, sizeof(*gem));
        }
    }

    DBG("DRM: RMFB fb_id=%u", fb_id);
    memset(fb, 0, sizeof(*fb));
    return 0;
}

static int drm_ioctl_mode_page_flip(drm_mode_page_flip_t *args) {
    if (!args)
        return -1;

    if (args->crtc_id != drm_dev.crtc.id)
        return -1;

    drm_framebuffer_t *fb = fb_find_by_id(args->fb_id);
    if (!fb)
        return -1;

    /* Update the CRTC's displayed framebuffer */
    drm_dev.crtc.fb_id = args->fb_id;

    /* Flip: copy the GEM buffer to the display backbuffer and present */
    drm_flip_fb(fb);

    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

void drm_init(void) {
    memset(&drm_dev, 0, sizeof(drm_dev));

    /* GEM/FB ID counters start at 1 (0 = invalid) */
    drm_dev.next_gem_handle = 1;
    drm_dev.next_fb_id = 1;

    /* VirtGPU 3D state */
    drm_dev.virgl_ctx_id = 0;
    drm_dev.virgl_ctx_created = 0;

    /* Detect backend */
    if (virtio_gpu_is_active()) {
        if (virtio_gpu_has_virgl()) {
            drm_dev.backend = DRM_BACKEND_VIRTIO_3D;
            DBG("DRM: VirtIO GPU with virgl 3D support");
        } else {
            drm_dev.backend = DRM_BACKEND_VIRTIO;
        }
        drm_dev.connector.type = DRM_MODE_CONNECTOR_VIRTUAL;
        drm_dev.encoder.type = DRM_MODE_ENCODER_VIRTUAL;

        uint32_t widths[4], heights[4];
        int n = virtio_gpu_get_display_info(widths, heights, 4);
        if (n > 0)
            drm_add_mode(widths[0], heights[0], 60, DRM_MODE_TYPE_PREFERRED);
    } else if (bga_detect()) {
        drm_dev.backend = DRM_BACKEND_BGA;
        drm_dev.connector.type = DRM_MODE_CONNECTOR_VGA;
        drm_dev.encoder.type = DRM_MODE_ENCODER_NONE;
    } else {
        drm_dev.backend = DRM_BACKEND_NONE;
        drm_dev.connector.type = DRM_MODE_CONNECTOR_Unknown;
        drm_dev.encoder.type = DRM_MODE_ENCODER_NONE;
    }

    uint32_t cur_w = gfx_width();
    uint32_t cur_h = gfx_height();
    if (cur_w > 0 && cur_h > 0) {
        uint32_t flags = (drm_dev.connector.num_modes == 0)
                         ? DRM_MODE_TYPE_PREFERRED : 0;
        drm_add_mode(cur_w, cur_h, 60, flags);
    }

    drm_add_mode(1920, 1080, 60, 0);
    drm_add_mode(1280,  720, 60, 0);
    drm_add_mode(1024,  768, 60, 0);
    drm_add_mode( 800,  600, 60, 0);

    drm_dev.crtc.id = 1;
    drm_dev.encoder.id = 1;
    drm_dev.encoder.crtc_id = 1;
    drm_dev.connector.id = 1;
    drm_dev.connector.encoder_id = 1;
    drm_dev.connector.connection = DRM_MODE_CONNECTED;

    if (cur_w > 0 && cur_h > 0) {
        drm_dev.connector.mm_width  = cur_w * 254 / 960;
        drm_dev.connector.mm_height = cur_h * 254 / 960;
    }

    if (drm_dev.connector.num_modes > 0) {
        drm_dev.crtc.mode_valid = 1;
        memcpy(&drm_dev.crtc.mode, &drm_dev.connector.modes[0],
               sizeof(drm_mode_modeinfo_t));
    }

    drm_dev.initialized = 1;
    DBG("DRM: initialized (Stage 2: GEM) backend=%d modes=%d",
        drm_dev.backend, drm_dev.connector.num_modes);
}

int drm_is_available(void) {
    return drm_dev.initialized;
}

drm_device_t *drm_get_device(void) {
    return drm_dev.initialized ? &drm_dev : NULL;
}

int drm_ioctl(uint32_t cmd, void *arg) {
    if (!drm_dev.initialized)
        return -1;

    /* Stage 0 */
    if (cmd == DRM_IOCTL_VERSION)
        return drm_ioctl_version((drm_version_t *)arg);
    if (cmd == DRM_IOCTL_GET_CAP)
        return drm_ioctl_get_cap((drm_get_cap_t *)arg);
    if (cmd == DRM_IOCTL_SET_CLIENT_CAP)
        return drm_ioctl_set_client_cap((drm_set_client_cap_t *)arg);
    if (cmd == DRM_IOCTL_GEM_CLOSE)
        return drm_ioctl_gem_close((drm_gem_close_t *)arg);

    /* Stage 1 KMS */
    if (cmd == DRM_IOCTL_MODE_GETRESOURCES)
        return drm_ioctl_mode_getresources((drm_mode_card_res_t *)arg);
    if (cmd == DRM_IOCTL_MODE_GETCONNECTOR)
        return drm_ioctl_mode_getconnector((drm_mode_get_connector_t *)arg);
    if (cmd == DRM_IOCTL_MODE_GETENCODER)
        return drm_ioctl_mode_getencoder((drm_mode_get_encoder_t *)arg);
    if (cmd == DRM_IOCTL_MODE_GETCRTC)
        return drm_ioctl_mode_getcrtc((drm_mode_crtc_t *)arg);
    if (cmd == DRM_IOCTL_MODE_SETCRTC)
        return drm_ioctl_mode_setcrtc((drm_mode_crtc_t *)arg);

    /* Stage 2 GEM */
    if (cmd == DRM_IOCTL_MODE_CREATE_DUMB)
        return drm_ioctl_mode_create_dumb((drm_mode_create_dumb_t *)arg);
    if (cmd == DRM_IOCTL_MODE_MAP_DUMB)
        return drm_ioctl_mode_map_dumb((drm_mode_map_dumb_t *)arg);
    if (cmd == DRM_IOCTL_MODE_DESTROY_DUMB)
        return drm_ioctl_mode_destroy_dumb((drm_mode_destroy_dumb_t *)arg);
    if (cmd == DRM_IOCTL_MODE_ADDFB)
        return drm_ioctl_mode_addfb((drm_mode_fb_cmd_t *)arg);
    if (cmd == DRM_IOCTL_MODE_RMFB)
        return drm_ioctl_mode_rmfb((uint32_t *)arg);
    if (cmd == DRM_IOCTL_MODE_PAGE_FLIP)
        return drm_ioctl_mode_page_flip((drm_mode_page_flip_t *)arg);

    /* VirtGPU 3D ioctls (nr 0x41..0x4B) */
    if (drm_dev.backend == DRM_BACKEND_VIRTIO_3D) {
        int rc = drm_virtgpu_ioctl(&drm_dev, cmd, arg);
        if (rc != -1 || _IOC_NR(cmd) >= 0x41)
            return rc;
    }

    printf("[DRM] Unknown ioctl cmd=0x%x (type='%c' nr=0x%x)\n",
           cmd, (char)_IOC_TYPE(cmd), _IOC_NR(cmd));
    return -1;
}
