#include <kernel/drm.h>
#include <kernel/ioctl.h>
#include <kernel/io.h>
#include <kernel/gfx.h>
#include <kernel/virtio_gpu.h>
#include <string.h>
#include <stdio.h>

/*
 * DRM Core — Stage 0 + Stage 1: ioctl dispatch, version, and KMS modesetting.
 *
 * All DRM operations go through ioctl() on /dev/dri/card0, which routes
 * here via ioctl_dispatch() -> drm_ioctl().
 *
 * Stage 0 implements:
 *   - DRM_IOCTL_VERSION: Returns driver name, version, description
 *   - DRM_IOCTL_GET_CAP: Reports supported capabilities
 *   - DRM_IOCTL_SET_CLIENT_CAP: Accepts client capability requests
 *
 * Stage 1 adds KMS modesetting:
 *   - DRM_IOCTL_MODE_GETRESOURCES: Lists CRTCs, connectors, encoders
 *   - DRM_IOCTL_MODE_GETCONNECTOR: Connector status + mode list
 *   - DRM_IOCTL_MODE_GETENCODER: Encoder -> CRTC mapping
 *   - DRM_IOCTL_MODE_GETCRTC: Current CRTC configuration
 *   - DRM_IOCTL_MODE_SETCRTC: Set display mode on a CRTC
 */

/* ── Driver identity ────────────────────────────────────────────── */

#define DRM_DRIVER_NAME     "impos-gpu"
#define DRM_DRIVER_DATE     "20260227"
#define DRM_DRIVER_DESC     "ImposOS DRM driver"
#define DRM_VERSION_MAJOR   0
#define DRM_VERSION_MINOR   2
#define DRM_VERSION_PATCH   0

/* ── Global DRM device state ────────────────────────────────────── */

static drm_device_t drm_dev;

/* ── Mode helpers ──────────────────────────────────────────────── */

/* Build a drm_mode_modeinfo_t from width/height/refresh.
 * We use simplified timings (no real sync pulses needed for VirtIO/BGA). */
static void drm_build_mode(drm_mode_modeinfo_t *m, uint32_t w, uint32_t h,
                            uint32_t refresh, uint32_t type_flags) {
    memset(m, 0, sizeof(*m));
    m->hdisplay = (uint16_t)w;
    m->vdisplay = (uint16_t)h;
    m->vrefresh = refresh;
    m->type = DRM_MODE_TYPE_DRIVER | type_flags;

    /* Simplified timings — not real VESA CVT, but sufficient for
     * VirtIO GPU and BGA which don't need real sync signals. */
    m->hsync_start = (uint16_t)(w + 48);
    m->hsync_end   = (uint16_t)(w + 48 + 112);
    m->htotal      = (uint16_t)(w + 48 + 112 + 80);
    m->vsync_start = (uint16_t)(h + 3);
    m->vsync_end   = (uint16_t)(h + 3 + 6);
    m->vtotal      = (uint16_t)(h + 3 + 6 + 25);
    m->clock       = (uint32_t)m->htotal * m->vtotal * refresh / 1000;

    snprintf(m->name, DRM_DISPLAY_MODE_LEN, "%ux%u", w, h);
}

/* Add a mode to the connector's mode list (if not already present) */
static int drm_add_mode(uint32_t w, uint32_t h, uint32_t refresh,
                         uint32_t type_flags) {
    if (drm_dev.connector.num_modes >= DRM_MAX_MODES)
        return -1;

    /* Check for duplicate */
    for (int i = 0; i < drm_dev.connector.num_modes; i++) {
        if (drm_dev.connector.modes[i].hdisplay == (uint16_t)w &&
            drm_dev.connector.modes[i].vdisplay == (uint16_t)h)
            return 0; /* already present */
    }

    drm_build_mode(&drm_dev.connector.modes[drm_dev.connector.num_modes],
                    w, h, refresh, type_flags);
    drm_dev.connector.num_modes++;
    return 0;
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

    /* Fill in counts — always 1 of each for single-display ImposOS */
    uint32_t crtc_id = drm_dev.crtc.id;
    uint32_t conn_id = drm_dev.connector.id;
    uint32_t enc_id  = drm_dev.encoder.id;

    /* Two-call pattern: if user provides arrays, fill them */
    if (res->crtc_id_ptr && res->count_crtcs >= 1)
        res->crtc_id_ptr[0] = crtc_id;
    if (res->connector_id_ptr && res->count_connectors >= 1)
        res->connector_id_ptr[0] = conn_id;
    if (res->encoder_id_ptr && res->count_encoders >= 1)
        res->encoder_id_ptr[0] = enc_id;

    /* Always set the counts */
    res->count_fbs = 0;         /* No framebuffers until Stage 2 */
    res->count_crtcs = 1;
    res->count_connectors = 1;
    res->count_encoders = 1;

    /* Resolution limits */
    res->min_width  = 640;
    res->max_width  = 1920;
    res->min_height = 480;
    res->max_height = 1080;

    return 0;
}

static int drm_ioctl_mode_getconnector(drm_mode_get_connector_t *conn) {
    if (!conn)
        return -1;

    /* Validate connector_id */
    if (conn->connector_id != drm_dev.connector.id)
        return -1;

    /* Copy modes into user array if provided (two-call pattern) */
    uint32_t num = (uint32_t)drm_dev.connector.num_modes;
    if (conn->modes_ptr && conn->count_modes > 0) {
        uint32_t copy = conn->count_modes < num ? conn->count_modes : num;
        memcpy(conn->modes_ptr, drm_dev.connector.modes,
               copy * sizeof(drm_mode_modeinfo_t));
    }

    /* Copy encoder list */
    if (conn->encoders_ptr && conn->count_encoders >= 1)
        conn->encoders_ptr[0] = drm_dev.encoder.id;

    /* Fill in connector info */
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
    enc->possible_crtcs = 1;    /* bit 0 = our single CRTC */
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
        /* Disable CRTC */
        drm_dev.crtc.mode_valid = 0;
        drm_dev.crtc.fb_id = 0;
        DBG("DRM: CRTC disabled");
        return 0;
    }

    /* Validate the requested mode against our mode list */
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

    /* Record the new mode */
    memcpy(&drm_dev.crtc.mode, &crtc->mode, sizeof(drm_mode_modeinfo_t));
    drm_dev.crtc.mode_valid = 1;
    drm_dev.crtc.fb_id = crtc->fb_id;
    drm_dev.crtc.x = crtc->x;
    drm_dev.crtc.y = crtc->y;

    /* Actually apply the mode change through the hardware backend.
     * For now, BGA can change modes; VirtIO scanout is set at init. */
    if (drm_dev.backend == DRM_BACKEND_BGA) {
        bga_set_mode((int)req_w, (int)req_h, 32);
        DBG("DRM: BGA mode set to %ux%u", req_w, req_h);
    } else {
        DBG("DRM: Mode recorded %ux%u (VirtIO scanout unchanged)", req_w, req_h);
    }

    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

void drm_init(void) {
    memset(&drm_dev, 0, sizeof(drm_dev));

    /* Detect backend and populate display info */
    if (virtio_gpu_is_active()) {
        drm_dev.backend = DRM_BACKEND_VIRTIO;
        drm_dev.connector.type = DRM_MODE_CONNECTOR_VIRTUAL;
        drm_dev.encoder.type = DRM_MODE_ENCODER_VIRTUAL;

        /* Query VirtIO display info for native mode */
        uint32_t widths[4], heights[4];
        int n = virtio_gpu_get_display_info(widths, heights, 4);
        if (n > 0) {
            /* First scanout's resolution is the preferred mode */
            drm_add_mode(widths[0], heights[0], 60, DRM_MODE_TYPE_PREFERRED);
        }
    } else if (bga_detect()) {
        drm_dev.backend = DRM_BACKEND_BGA;
        drm_dev.connector.type = DRM_MODE_CONNECTOR_VGA;
        drm_dev.encoder.type = DRM_MODE_ENCODER_NONE;
    } else {
        drm_dev.backend = DRM_BACKEND_NONE;
        drm_dev.connector.type = DRM_MODE_CONNECTOR_Unknown;
        drm_dev.encoder.type = DRM_MODE_ENCODER_NONE;
    }

    /* Always add the current framebuffer resolution as a mode */
    uint32_t cur_w = gfx_width();
    uint32_t cur_h = gfx_height();
    if (cur_w > 0 && cur_h > 0) {
        /* If no VirtIO modes were added, this becomes the preferred mode */
        uint32_t flags = (drm_dev.connector.num_modes == 0)
                         ? DRM_MODE_TYPE_PREFERRED : 0;
        drm_add_mode(cur_w, cur_h, 60, flags);
    }

    /* Add common standard modes (BGA and VirtIO both support these) */
    drm_add_mode(1920, 1080, 60, 0);
    drm_add_mode(1280,  720, 60, 0);
    drm_add_mode(1024,  768, 60, 0);
    drm_add_mode( 800,  600, 60, 0);

    /* Set up the single CRTC/encoder/connector pipeline */
    drm_dev.crtc.id = 1;
    drm_dev.encoder.id = 1;
    drm_dev.encoder.crtc_id = 1;
    drm_dev.connector.id = 1;
    drm_dev.connector.encoder_id = 1;
    drm_dev.connector.connection = DRM_MODE_CONNECTED;

    /* Physical size estimate (~24" monitor at current resolution) */
    if (cur_w > 0 && cur_h > 0) {
        drm_dev.connector.mm_width  = cur_w * 254 / 960;  /* ~96 DPI */
        drm_dev.connector.mm_height = cur_h * 254 / 960;
    }

    /* Set current CRTC mode to the active framebuffer resolution */
    if (drm_dev.connector.num_modes > 0) {
        drm_dev.crtc.mode_valid = 1;
        memcpy(&drm_dev.crtc.mode, &drm_dev.connector.modes[0],
               sizeof(drm_mode_modeinfo_t));
    }

    drm_dev.initialized = 1;
    DBG("DRM: initialized (Stage 1: KMS) backend=%d modes=%d",
        drm_dev.backend, drm_dev.connector.num_modes);
}

int drm_is_available(void) {
    return drm_dev.initialized;
}

int drm_ioctl(uint32_t cmd, void *arg) {
    if (!drm_dev.initialized)
        return -1;

    /* Stage 0 ioctls */
    if (cmd == DRM_IOCTL_VERSION)
        return drm_ioctl_version((drm_version_t *)arg);
    if (cmd == DRM_IOCTL_GET_CAP)
        return drm_ioctl_get_cap((drm_get_cap_t *)arg);
    if (cmd == DRM_IOCTL_SET_CLIENT_CAP)
        return drm_ioctl_set_client_cap((drm_set_client_cap_t *)arg);

    /* Stage 1 KMS ioctls */
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

    /* Unknown ioctl */
    printf("[DRM] Unknown ioctl cmd=0x%x (type='%c' nr=0x%x)\n",
           cmd, (char)_IOC_TYPE(cmd), _IOC_NR(cmd));
    return -1;
}
