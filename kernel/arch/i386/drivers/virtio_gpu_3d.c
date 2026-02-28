#include <kernel/virtio_gpu_3d.h>
#include <kernel/virtio_gpu.h>
#include <kernel/virtio_gpu_internal.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/*
 * VirtIO GPU 3D (virgl) command wrappers.
 *
 * These build the protocol structs and submit them via the public
 * virtio_gpu_submit_ctrl_cmd() / _data() helpers from virtio_gpu.c.
 */

/* Static command/response buffers for 3D operations.
   Separate from the 2D buffers to avoid conflicts. */
static uint8_t cmd3d_buf[512] __attribute__((aligned(64)));
static uint8_t resp3d_buf[256] __attribute__((aligned(64)));

/* ═══ Context management ══════════════════════════════════════ */

int virtio_gpu_3d_ctx_create(uint32_t ctx_id, const char *name) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_ctx_create *cmd =
        (struct virtio_gpu_ctx_create *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_CREATE;
    cmd->hdr.ctx_id = ctx_id;
    cmd->context_init = 0; /* virgl */

    if (name) {
        uint32_t len = 0;
        while (name[len] && len < 63) len++;
        memcpy(cmd->debug_name, name, len);
        cmd->nlen = len;
    }

    int rc = virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                         resp3d_buf,
                                         sizeof(struct virtio_gpu_ctrl_hdr));
    if (rc == 0) {
        DBG("[virgl] ctx %u created (%s)", ctx_id, name ? name : "");
    } else {
        DBG("[virgl] ctx %u create FAILED", ctx_id);
    }
    return rc;
}

int virtio_gpu_3d_ctx_destroy(uint32_t ctx_id) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_ctx_destroy *cmd =
        (struct virtio_gpu_ctx_destroy *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DESTROY;
    cmd->hdr.ctx_id = ctx_id;

    return virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                       resp3d_buf,
                                       sizeof(struct virtio_gpu_ctrl_hdr));
}

/* ═══ 3D Resource creation ════════════════════════════════════ */

int virtio_gpu_3d_resource_create(uint32_t ctx_id, uint32_t res_id,
                                   uint32_t target, uint32_t format,
                                   uint32_t bind, uint32_t width,
                                   uint32_t height, uint32_t depth,
                                   uint32_t array_size, uint32_t last_level,
                                   uint32_t nr_samples, uint32_t flags) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_resource_create_3d *cmd =
        (struct virtio_gpu_resource_create_3d *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_3D;
    cmd->hdr.ctx_id = ctx_id;
    cmd->resource_id = res_id;
    cmd->target = target;
    cmd->format = format;
    cmd->bind = bind;
    cmd->width = width;
    cmd->height = height;
    cmd->depth = depth;
    cmd->array_size = array_size;
    cmd->last_level = last_level;
    cmd->nr_samples = nr_samples;
    cmd->flags = flags;

    int rc = virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                         resp3d_buf,
                                         sizeof(struct virtio_gpu_ctrl_hdr));
    if (rc == 0) {
        DBG("[virgl] resource %u created (%ux%ux%u target=%u fmt=%u bind=0x%x)",
            res_id, width, height, depth, target, format, bind);
    }
    return rc;
}

/* ═══ Context resource attachment ═════════════════════════════ */

int virtio_gpu_3d_ctx_attach_resource(uint32_t ctx_id, uint32_t res_id) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_ctx_resource *cmd =
        (struct virtio_gpu_ctx_resource *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE;
    cmd->hdr.ctx_id = ctx_id;
    cmd->resource_id = res_id;

    return virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                       resp3d_buf,
                                       sizeof(struct virtio_gpu_ctrl_hdr));
}

int virtio_gpu_3d_ctx_detach_resource(uint32_t ctx_id, uint32_t res_id) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_ctx_resource *cmd =
        (struct virtio_gpu_ctx_resource *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE;
    cmd->hdr.ctx_id = ctx_id;
    cmd->resource_id = res_id;

    return virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                       resp3d_buf,
                                       sizeof(struct virtio_gpu_ctrl_hdr));
}

/* ═══ 3D Transfers ════════════════════════════════════════════ */

int virtio_gpu_3d_transfer_to_host(uint32_t res_id, uint32_t ctx_id,
                                    uint32_t level, uint32_t stride,
                                    uint32_t layer_stride,
                                    struct virtio_gpu_box *box,
                                    uint64_t offset) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_transfer_host_3d *cmd =
        (struct virtio_gpu_transfer_host_3d *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D;
    cmd->hdr.ctx_id = ctx_id;
    cmd->resource_id = res_id;
    cmd->level = level;
    cmd->stride = stride;
    cmd->layer_stride = layer_stride;
    cmd->offset = offset;
    if (box)
        memcpy(&cmd->box, box, sizeof(struct virtio_gpu_box));

    return virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                       resp3d_buf,
                                       sizeof(struct virtio_gpu_ctrl_hdr));
}

int virtio_gpu_3d_transfer_from_host(uint32_t res_id, uint32_t ctx_id,
                                      uint32_t level, uint32_t stride,
                                      uint32_t layer_stride,
                                      struct virtio_gpu_box *box,
                                      uint64_t offset) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_transfer_host_3d *cmd =
        (struct virtio_gpu_transfer_host_3d *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D;
    cmd->hdr.ctx_id = ctx_id;
    cmd->resource_id = res_id;
    cmd->level = level;
    cmd->stride = stride;
    cmd->layer_stride = layer_stride;
    cmd->offset = offset;
    if (box)
        memcpy(&cmd->box, box, sizeof(struct virtio_gpu_box));

    return virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                       resp3d_buf,
                                       sizeof(struct virtio_gpu_ctrl_hdr));
}

/* ═══ Gallium command stream submission ═══════════════════════ */

int virtio_gpu_3d_submit(uint32_t ctx_id, void *cmd_data, uint32_t cmd_len) {
    if (!virtio_gpu_has_virgl()) return -1;
    if (!cmd_data || cmd_len == 0) return -1;

    struct virtio_gpu_cmd_submit *cmd =
        (struct virtio_gpu_cmd_submit *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_SUBMIT_3D;
    cmd->hdr.ctx_id = ctx_id;
    cmd->size = cmd_len;

    /* Use the 3-descriptor chain: header + command data + response */
    return virtio_gpu_submit_ctrl_cmd_data(cmd, sizeof(*cmd),
                                            cmd_data, cmd_len,
                                            resp3d_buf,
                                            sizeof(struct virtio_gpu_ctrl_hdr));
}

/* ═══ Capability set queries ══════════════════════════════════ */

int virtio_gpu_3d_get_capset_info(uint32_t index,
                                   uint32_t *capset_id,
                                   uint32_t *capset_max_version,
                                   uint32_t *capset_max_size) {
    if (!virtio_gpu_has_virgl()) return -1;

    struct virtio_gpu_get_capset_info *cmd =
        (struct virtio_gpu_get_capset_info *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET_INFO;
    cmd->capset_index = index;

    struct virtio_gpu_resp_capset_info *resp =
        (struct virtio_gpu_resp_capset_info *)resp3d_buf;
    memset(resp, 0, sizeof(*resp));

    int rc = virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                         resp, sizeof(*resp));
    if (rc != 0) return -1;
    if (resp->hdr.type != VIRTIO_GPU_RESP_OK_CAPSET_INFO) return -1;

    if (capset_id) *capset_id = resp->capset_id;
    if (capset_max_version) *capset_max_version = resp->capset_max_version;
    if (capset_max_size) *capset_max_size = resp->capset_max_size;

    DBG("[virgl] capset[%u]: id=%u ver=%u size=%u",
        index, resp->capset_id, resp->capset_max_version,
        resp->capset_max_size);
    return 0;
}

int virtio_gpu_3d_get_capset(uint32_t capset_id, uint32_t version,
                              void *buf, uint32_t buf_len) {
    if (!virtio_gpu_has_virgl()) return -1;
    if (!buf || buf_len == 0) return -1;

    struct virtio_gpu_get_capset *cmd =
        (struct virtio_gpu_get_capset *)cmd3d_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_GET_CAPSET;
    cmd->capset_id = capset_id;
    cmd->capset_version = version;

    /* Response: ctrl_hdr + capset data.
       We need a response buffer large enough for hdr + buf_len.
       Use resp3d_buf if it fits, otherwise the caller's buffer won't work.
       For simplicity, cap at 256 - sizeof(ctrl_hdr) = 232 bytes. */
    uint32_t resp_size = sizeof(struct virtio_gpu_ctrl_hdr) + buf_len;
    if (resp_size > sizeof(resp3d_buf)) {
        DBG("[virgl] capset too large (%u > %u)", resp_size,
            (uint32_t)sizeof(resp3d_buf));
        return -1;
    }

    memset(resp3d_buf, 0, resp_size);
    int rc = virtio_gpu_submit_ctrl_cmd(cmd, sizeof(*cmd),
                                         resp3d_buf, resp_size);
    if (rc != 0) return -1;

    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp3d_buf;
    if (hdr->type != VIRTIO_GPU_RESP_OK_CAPSET) return -1;

    /* Copy capset data after the header */
    memcpy(buf, resp3d_buf + sizeof(struct virtio_gpu_ctrl_hdr), buf_len);
    return 0;
}
