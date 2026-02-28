#include <kernel/drm.h>
#include <kernel/virtio_gpu.h>
#include <kernel/virtio_gpu_3d.h>
#include <kernel/virtio_gpu_internal.h>
#include <kernel/pmm.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/*
 * DRM VirtGPU ioctl handlers.
 *
 * These implement the DRM_VIRTGPU_* ioctls that match Linux's
 * virtio_gpu_drm driver, allowing future Mesa integration.
 *
 * The ioctls bridge between the DRM GEM object model and the
 * VirtIO GPU 3D command set (virgl).
 */

#define PAGE_SIZE 4096

/* ── GEM helpers (access drm_device internals) ─────────────────── */

static drm_gem_object_t *gem_find(drm_device_t *dev, uint32_t handle) {
    for (int i = 0; i < DRM_GEM_MAX_OBJECTS; i++) {
        if (dev->gem_objects[i].in_use &&
            dev->gem_objects[i].handle == handle)
            return &dev->gem_objects[i];
    }
    return NULL;
}

static drm_gem_object_t *gem_alloc(drm_device_t *dev) {
    for (int i = 0; i < DRM_GEM_MAX_OBJECTS; i++) {
        if (!dev->gem_objects[i].in_use)
            return &dev->gem_objects[i];
    }
    return NULL;
}

/* ── Ensure virgl context exists ───────────────────────────────── */

static int ensure_virgl_ctx(drm_device_t *dev) {
    if (dev->virgl_ctx_created) return 0;

    dev->virgl_ctx_id = 1; /* single context for now */
    int rc = virtio_gpu_3d_ctx_create(dev->virgl_ctx_id, "drm-ctx");
    if (rc != 0) return -1;

    dev->virgl_ctx_created = 1;
    return 0;
}

/* ── CONTEXT_INIT ──────────────────────────────────────────────── */

static int virtgpu_context_init(drm_device_t *dev,
                                 drm_virtgpu_context_init_t *args) {
    (void)args; /* params unused for basic virgl */
    return ensure_virgl_ctx(dev);
}

/* ── GETPARAM ──────────────────────────────────────────────────── */

static int virtgpu_getparam(drm_device_t *dev,
                             drm_virtgpu_getparam_t *args) {
    (void)dev;
    if (!args) return -1;

    switch (args->param) {
    case VIRTGPU_PARAM_3D_FEATURES:
        args->value = virtio_gpu_has_virgl() ? 1 : 0;
        return 0;
    case VIRTGPU_PARAM_CAPSET_QUERY_FIX:
        args->value = 1;
        return 0;
    default:
        args->value = 0;
        return 0;
    }
}

/* ── RESOURCE_CREATE ───────────────────────────────────────────── */

static int virtgpu_resource_create(drm_device_t *dev,
                                    drm_virtgpu_resource_create_t *args) {
    if (!args) return -1;

    /* Ensure virgl context */
    if (ensure_virgl_ctx(dev) != 0) return -1;

    /* Allocate resource ID */
    uint32_t res_id = virtio_gpu_alloc_resource_id();

    /* Calculate buffer size */
    uint32_t stride = args->width * 4; /* assume 32bpp */
    uint32_t size = stride * args->height;
    if (args->target == PIPE_BUFFER) {
        size = args->width; /* PIPE_BUFFER: width = byte size */
        stride = 0;
    }
    if (size == 0) size = PAGE_SIZE;

    /* Allocate PMM-backed memory */
    uint32_t n_frames = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t phys = pmm_alloc_contiguous(n_frames);
    if (!phys) {
        DBG("[virtgpu-drm] RESOURCE_CREATE: alloc failed (%u frames)", n_frames);
        return -1;
    }
    memset((void *)phys, 0, n_frames * PAGE_SIZE);

    /* Create 3D resource on GPU */
    int rc = virtio_gpu_3d_resource_create(
        dev->virgl_ctx_id, res_id,
        args->target, args->format, args->bind,
        args->width, args->height, args->depth,
        args->array_size, args->last_level,
        args->nr_samples, args->flags);
    if (rc != 0) {
        pmm_free_contiguous(phys, n_frames);
        return -1;
    }

    /* Attach backing storage */
    rc = virtio_gpu_attach_resource_backing(res_id, (uint32_t *)phys, size);
    if (rc != 0) {
        pmm_free_contiguous(phys, n_frames);
        return -1;
    }

    /* Attach resource to virgl context */
    virtio_gpu_3d_ctx_attach_resource(dev->virgl_ctx_id, res_id);

    /* Create GEM object */
    drm_gem_object_t *gem = gem_alloc(dev);
    if (!gem) {
        pmm_free_contiguous(phys, n_frames);
        return -1;
    }

    gem->in_use = 1;
    gem->handle = dev->next_gem_handle++;
    gem->phys_addr = phys;
    gem->size = size;
    gem->n_frames = n_frames;
    gem->width = args->width;
    gem->height = args->height;
    gem->pitch = stride;
    gem->bpp = 32;
    gem->refcount = 1;
    gem->res_id = res_id;

    /* Return to caller */
    args->bo_handle = gem->handle;
    args->res_handle = res_id;
    args->size = size;
    args->stride = stride;

    DBG("[virtgpu-drm] RESOURCE_CREATE: handle=%u res=%u %ux%u (phys=0x%x)",
        gem->handle, res_id, args->width, args->height, phys);
    return 0;
}

/* ── MAP ───────────────────────────────────────────────────────── */

static int virtgpu_map(drm_device_t *dev, drm_virtgpu_map_t *args) {
    if (!args) return -1;

    drm_gem_object_t *gem = gem_find(dev, args->handle);
    if (!gem) return -1;

    /* Identity-mapped kernel: phys == virt */
    args->offset = (uint64_t)gem->phys_addr;
    return 0;
}

/* ── EXECBUFFER ────────────────────────────────────────────────── */

static int virtgpu_execbuffer(drm_device_t *dev,
                               drm_virtgpu_execbuffer_t *args) {
    if (!args || !args->command || args->size == 0) return -1;

    /* Auto-create context on first exec (matches Linux behavior) */
    if (ensure_virgl_ctx(dev) != 0) return -1;

    return virtio_gpu_3d_submit(dev->virgl_ctx_id,
                                 (void *)(uint32_t)args->command,
                                 args->size);
}

/* ── TRANSFER_TO_HOST ──────────────────────────────────────────── */

static int virtgpu_transfer_to_host(drm_device_t *dev,
                                     drm_virtgpu_3d_transfer_t *args) {
    if (!args) return -1;

    drm_gem_object_t *gem = gem_find(dev, args->bo_handle);
    if (!gem || !gem->res_id) return -1;

    if (ensure_virgl_ctx(dev) != 0) return -1;

    struct virtio_gpu_box box;
    box.x = args->x; box.y = args->y; box.z = args->z;
    box.w = args->w; box.h = args->h; box.d = args->d;

    return virtio_gpu_3d_transfer_to_host(
        gem->res_id, dev->virgl_ctx_id,
        args->level, args->stride, args->layer_stride,
        &box, args->offset);
}

/* ── TRANSFER_FROM_HOST ────────────────────────────────────────── */

static int virtgpu_transfer_from_host(drm_device_t *dev,
                                       drm_virtgpu_3d_transfer_t *args) {
    if (!args) return -1;

    drm_gem_object_t *gem = gem_find(dev, args->bo_handle);
    if (!gem || !gem->res_id) return -1;

    if (ensure_virgl_ctx(dev) != 0) return -1;

    struct virtio_gpu_box box;
    box.x = args->x; box.y = args->y; box.z = args->z;
    box.w = args->w; box.h = args->h; box.d = args->d;

    return virtio_gpu_3d_transfer_from_host(
        gem->res_id, dev->virgl_ctx_id,
        args->level, args->stride, args->layer_stride,
        &box, args->offset);
}

/* ── WAIT ──────────────────────────────────────────────────────── */

static int virtgpu_wait(drm_device_t *dev, drm_virtgpu_wait_t *args) {
    (void)dev;
    (void)args;
    /* Our submission is synchronous (polling), so WAIT is a no-op */
    return 0;
}

/* ── GET_CAPS ──────────────────────────────────────────────────── */

static int virtgpu_get_caps(drm_device_t *dev,
                             drm_virtgpu_get_caps_t *args) {
    (void)dev;
    if (!args || !args->addr || args->size == 0) return -1;

    return virtio_gpu_3d_get_capset(args->cap_set_id, args->cap_set_ver,
                                     (void *)(uint32_t)args->addr,
                                     args->size);
}

/* ── RESOURCE_INFO ─────────────────────────────────────────────── */

static int virtgpu_resource_info(drm_device_t *dev,
                                  drm_virtgpu_resource_info_t *args) {
    if (!args) return -1;

    drm_gem_object_t *gem = gem_find(dev, args->bo_handle);
    if (!gem) return -1;

    args->res_handle = gem->res_id;
    args->size = gem->size;
    args->stride = gem->pitch;
    return 0;
}

/* ═══ Public dispatch ══════════════════════════════════════════ */

int drm_virtgpu_ioctl(drm_device_t *dev, uint32_t cmd, void *arg) {
    if (!virtio_gpu_has_virgl()) return -1;

    if (cmd == DRM_IOCTL_VIRTGPU_CONTEXT_INIT)
        return virtgpu_context_init(dev, (drm_virtgpu_context_init_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_GETPARAM)
        return virtgpu_getparam(dev, (drm_virtgpu_getparam_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_RESOURCE_CREATE)
        return virtgpu_resource_create(dev, (drm_virtgpu_resource_create_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_MAP)
        return virtgpu_map(dev, (drm_virtgpu_map_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_EXECBUFFER)
        return virtgpu_execbuffer(dev, (drm_virtgpu_execbuffer_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_TRANSFER_TO_HOST)
        return virtgpu_transfer_to_host(dev, (drm_virtgpu_3d_transfer_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_TRANSFER_FROM_HOST)
        return virtgpu_transfer_from_host(dev, (drm_virtgpu_3d_transfer_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_WAIT)
        return virtgpu_wait(dev, (drm_virtgpu_wait_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_GET_CAPS)
        return virtgpu_get_caps(dev, (drm_virtgpu_get_caps_t *)arg);
    if (cmd == DRM_IOCTL_VIRTGPU_RESOURCE_INFO)
        return virtgpu_resource_info(dev, (drm_virtgpu_resource_info_t *)arg);

    return -1;
}
