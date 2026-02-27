#include <kernel/drm.h>
#include <kernel/ioctl.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/*
 * DRM Core — Stage 0: ioctl dispatch and version query.
 *
 * This is the foundation of the DRM subsystem. All DRM operations go through
 * ioctl() on /dev/dri/card0, which routes here via ioctl_dispatch() → drm_ioctl().
 *
 * Stage 0 implements:
 *   - DRM_IOCTL_VERSION: Returns driver name, version, and description
 *   - DRM_IOCTL_GET_CAP: Reports supported capabilities (dumb buffers, etc.)
 *   - DRM_IOCTL_SET_CLIENT_CAP: Accepts client capability requests
 *
 * Later stages add:
 *   - Stage 1: KMS mode-setting ioctls (GETRESOURCES, SETCRTC, etc.)
 *   - Stage 2: GEM buffer management (CREATE_DUMB, MAP_DUMB, ADDFB)
 *   - Stage 4a: VirtIO GPU 3D ioctls (EXECBUFFER, RESOURCE_CREATE, etc.)
 */

/* ── Driver identity ────────────────────────────────────────────── */

#define DRM_DRIVER_NAME     "impos-gpu"
#define DRM_DRIVER_DATE     "20260227"
#define DRM_DRIVER_DESC     "ImposOS DRM driver"
#define DRM_VERSION_MAJOR   0
#define DRM_VERSION_MINOR   1
#define DRM_VERSION_PATCH   0

/* ── Global DRM device state ────────────────────────────────────── */

static drm_device_t drm_dev;

/* ── ioctl handlers ─────────────────────────────────────────────── */

static int drm_ioctl_version(drm_version_t *ver) {
    if (!ver)
        return -1;

    ver->version_major = DRM_VERSION_MAJOR;
    ver->version_minor = DRM_VERSION_MINOR;
    ver->version_patchlevel = DRM_VERSION_PATCH;

    /* Copy driver name (truncate if buffer too small) */
    uint32_t name_len = sizeof(DRM_DRIVER_NAME) - 1;
    if (ver->name && ver->name_len > 0) {
        uint32_t copy_len = ver->name_len < name_len ? ver->name_len : name_len;
        memcpy(ver->name, DRM_DRIVER_NAME, copy_len);
        if (copy_len < ver->name_len)
            ver->name[copy_len] = '\0';
    }
    ver->name_len = name_len;

    /* Copy date */
    uint32_t date_len = sizeof(DRM_DRIVER_DATE) - 1;
    if (ver->date && ver->date_len > 0) {
        uint32_t copy_len = ver->date_len < date_len ? ver->date_len : date_len;
        memcpy(ver->date, DRM_DRIVER_DATE, copy_len);
        if (copy_len < ver->date_len)
            ver->date[copy_len] = '\0';
    }
    ver->date_len = date_len;

    /* Copy description */
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
            cap->value = 1;  /* We will support dumb buffers in Stage 2 */
            return 0;

        case DRM_CAP_PRIME:
            cap->value = 0;  /* No DMA-BUF/PRIME support */
            return 0;

        case DRM_CAP_TIMESTAMP_MONOTONIC:
            cap->value = 1;  /* PIT ticks are monotonic */
            return 0;

        default:
            cap->value = 0;
            return 0;
    }
}

static int drm_ioctl_set_client_cap(drm_set_client_cap_t *cap) {
    if (!cap)
        return -1;

    /* Accept all client caps for now — we'll enforce them in Stage 1 */
    switch (cap->capability) {
        case DRM_CLIENT_CAP_UNIVERSAL_PLANES:
        case DRM_CLIENT_CAP_ATOMIC:
            return 0;
        default:
            return -1;
    }
}

static int drm_ioctl_mode_getresources(drm_mode_card_res_t *res) {
    if (!res)
        return -1;

    /* Stage 0: report zero resources. Stage 1 fills these in. */
    res->count_fbs = 0;
    res->count_crtcs = 0;
    res->count_connectors = 0;
    res->count_encoders = 0;
    res->min_width = 0;
    res->max_width = 0;
    res->min_height = 0;
    res->max_height = 0;

    return 0;
}

/* ── Public API ─────────────────────────────────────────────────── */

void drm_init(void) {
    memset(&drm_dev, 0, sizeof(drm_dev));
    drm_dev.initialized = 1;
    DBG("DRM: initialized (Stage 0: ioctl dispatch)");
}

int drm_is_available(void) {
    return drm_dev.initialized;
}

int drm_ioctl(uint32_t cmd, void *arg) {
    if (!drm_dev.initialized)
        return -1;

    /* Dispatch by ioctl command number.
     * We compare the full encoded command (direction + size + type + nr). */
    if (cmd == DRM_IOCTL_VERSION)
        return drm_ioctl_version((drm_version_t *)arg);

    if (cmd == DRM_IOCTL_GET_CAP)
        return drm_ioctl_get_cap((drm_get_cap_t *)arg);

    if (cmd == DRM_IOCTL_SET_CLIENT_CAP)
        return drm_ioctl_set_client_cap((drm_set_client_cap_t *)arg);

    if (cmd == DRM_IOCTL_MODE_GETRESOURCES)
        return drm_ioctl_mode_getresources((drm_mode_card_res_t *)arg);

    /* Unknown ioctl */
    printf("[DRM] Unknown ioctl cmd=0x%x (type='%c' nr=0x%x)\n",
           cmd, (char)_IOC_TYPE(cmd), _IOC_NR(cmd));
    return -1;
}
