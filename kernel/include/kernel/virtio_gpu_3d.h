#ifndef _KERNEL_VIRTIO_GPU_3D_H
#define _KERNEL_VIRTIO_GPU_3D_H

#include <stdint.h>
#include <kernel/virtio_gpu_internal.h>

/*
 * VirtIO GPU 3D (virgl) command API.
 *
 * These functions submit 3D commands to the host GPU via VirtIO GPU.
 * Requires VIRTIO_GPU_F_VIRGL to be negotiated during init.
 */

/* Check if virgl 3D is available (feature was negotiated) */
int virtio_gpu_has_virgl(void);

/* Context management */
int virtio_gpu_3d_ctx_create(uint32_t ctx_id, const char *name);
int virtio_gpu_3d_ctx_destroy(uint32_t ctx_id);

/* 3D resource creation */
int virtio_gpu_3d_resource_create(uint32_t ctx_id, uint32_t res_id,
                                   uint32_t target, uint32_t format,
                                   uint32_t bind, uint32_t width,
                                   uint32_t height, uint32_t depth,
                                   uint32_t array_size, uint32_t last_level,
                                   uint32_t nr_samples, uint32_t flags);

/* Context resource attachment */
int virtio_gpu_3d_ctx_attach_resource(uint32_t ctx_id, uint32_t res_id);
int virtio_gpu_3d_ctx_detach_resource(uint32_t ctx_id, uint32_t res_id);

/* 3D transfers (upload/readback) */
int virtio_gpu_3d_transfer_to_host(uint32_t res_id, uint32_t ctx_id,
                                    uint32_t level, uint32_t stride,
                                    uint32_t layer_stride,
                                    struct virtio_gpu_box *box,
                                    uint64_t offset);
int virtio_gpu_3d_transfer_from_host(uint32_t res_id, uint32_t ctx_id,
                                      uint32_t level, uint32_t stride,
                                      uint32_t layer_stride,
                                      struct virtio_gpu_box *box,
                                      uint64_t offset);

/* Submit Gallium3D command stream */
int virtio_gpu_3d_submit(uint32_t ctx_id, void *cmd_buf, uint32_t cmd_len);

/* Capability set queries */
int virtio_gpu_3d_get_capset_info(uint32_t index,
                                   uint32_t *capset_id,
                                   uint32_t *capset_max_version,
                                   uint32_t *capset_max_size);
int virtio_gpu_3d_get_capset(uint32_t capset_id, uint32_t version,
                              void *buf, uint32_t buf_len);

/* Resource ID allocation (shared with 2D) */
uint32_t virtio_gpu_alloc_resource_id(void);

/* Attach backing storage to any resource (wraps existing 2D mechanism) */
int virtio_gpu_attach_resource_backing(uint32_t res_id,
                                        uint32_t *buf, uint32_t size_bytes);

/* Set scanout to a resource */
int virtio_gpu_set_scanout_resource(uint32_t res_id, uint32_t scanout_id,
                                     uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h);

/* Flush a resource to display */
int virtio_gpu_flush_resource(uint32_t res_id,
                               uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h);

#endif /* _KERNEL_VIRTIO_GPU_3D_H */
