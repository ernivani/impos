#ifndef _KERNEL_VIRTIO_GPU_INTERNAL_H
#define _KERNEL_VIRTIO_GPU_INTERNAL_H

#include <stdint.h>

/*
 * VirtIO GPU internal protocol structures and command IDs.
 *
 * Shared between virtio_gpu.c (2D) and virtio_gpu_3d.c (3D/virgl).
 * Based on VirtIO GPU spec sections 5.7.6.7 and 5.7.6.8.
 */

/* ═══ Feature bits ═════════════════════════════════════════════ */

#define VIRTIO_GPU_F_VIRGL              0   /* bit 0: 3D virgl support */
#define VIRTIO_GPU_F_EDID               1   /* bit 1: EDID support */

/* ═══ 2D command types (spec 5.7.6.7) ═════════════════════════ */

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF           0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  0x0107
#define VIRTIO_GPU_CMD_GET_CAPSET_INFO          0x0108
#define VIRTIO_GPU_CMD_GET_CAPSET               0x0109

/* ═══ 3D command types (spec 5.7.6.8) ═════════════════════════ */

#define VIRTIO_GPU_CMD_CTX_CREATE               0x0200
#define VIRTIO_GPU_CMD_CTX_DESTROY              0x0201
#define VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE      0x0202
#define VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE      0x0203
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_3D       0x0204
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D      0x0205
#define VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D    0x0206
#define VIRTIO_GPU_CMD_SUBMIT_3D                0x0207

/* ═══ Cursor command types ═════════════════════════════════════ */

#define VIRTIO_GPU_CMD_UPDATE_CURSOR            0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR              0x0301

/* ═══ Response types ═══════════════════════════════════════════ */

#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101
#define VIRTIO_GPU_RESP_OK_CAPSET_INFO          0x1102
#define VIRTIO_GPU_RESP_OK_CAPSET               0x1103

#define VIRTIO_GPU_RESP_ERR_UNSPEC              0x1200
#define VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY       0x1201
#define VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID  0x1202
#define VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID 0x1203
#define VIRTIO_GPU_RESP_ERR_INVALID_CONTEXT_ID  0x1204
#define VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER   0x1205

/* ═══ Pixel format ═════════════════════════════════════════════ */

#define VIRTIO_GPU_FORMAT_B8G8R8A8_UNORM        1
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM        2
#define VIRTIO_GPU_FORMAT_A8R8G8B8_UNORM        3
#define VIRTIO_GPU_FORMAT_X8R8G8B8_UNORM        4
#define VIRTIO_GPU_FORMAT_R8G8B8A8_UNORM        67
#define VIRTIO_GPU_FORMAT_R8G8B8X8_UNORM        68

/* ═══ Control header flags ═════════════════════════════════════ */

#define VIRTIO_GPU_FLAG_FENCE                   (1 << 0)

/* ═══ Protocol structures ══════════════════════════════════════ */

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x, y, width, height;
} __attribute__((packed));

struct virtio_gpu_box {
    uint32_t x, y, z, w, h, d;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

/* ═══ 2D protocol structures ══════════════════════════════════ */

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush_cmd {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_unref {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* ═══ 3D protocol structures ══════════════════════════════════ */

struct virtio_gpu_ctx_create {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t nlen;
    uint32_t context_init;   /* 0 for virgl */
    char debug_name[64];
} __attribute__((packed));

struct virtio_gpu_ctx_destroy {
    struct virtio_gpu_ctrl_hdr hdr;
} __attribute__((packed));

struct virtio_gpu_resource_create_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t target;         /* PIPE_TEXTURE_2D=2, PIPE_BUFFER=0 */
    uint32_t format;         /* VIRGL_FORMAT_B8G8R8X8_UNORM etc */
    uint32_t bind;           /* VIRGL_BIND_RENDER_TARGET etc */
    uint32_t width;
    uint32_t height;
    uint32_t depth;
    uint32_t array_size;
    uint32_t last_level;
    uint32_t nr_samples;
    uint32_t flags;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_cmd_submit {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t size;           /* byte length of following command data */
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_transfer_host_3d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_box box;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t level;
    uint32_t stride;
    uint32_t layer_stride;
} __attribute__((packed));

struct virtio_gpu_ctx_resource {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* ═══ Capset structures ═══════════════════════════════════════ */

struct virtio_gpu_get_capset_info {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t capset_index;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resp_capset_info {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t capset_id;
    uint32_t capset_max_version;
    uint32_t capset_max_size;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_get_capset {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t capset_id;
    uint32_t capset_version;
} __attribute__((packed));

/* Response for GET_CAPSET is variable-length:
   struct virtio_gpu_ctrl_hdr hdr + capset_max_size bytes of data */

/* ═══ Display info structures ═════════════════════════════════ */

#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

/* ═══ Cursor structures ═══════════════════════════════════════ */

struct virtio_gpu_cursor_cmd {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        uint32_t scanout_id;
        uint32_t x;
        uint32_t y;
        uint32_t padding;
    } pos;
    uint32_t resource_id;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t padding;
} __attribute__((packed));

/* ═══ Virgl (Gallium3D) constants ═════════════════════════════ */

/* Pipe texture targets */
#define PIPE_BUFFER             0
#define PIPE_TEXTURE_1D         1
#define PIPE_TEXTURE_2D         2
#define PIPE_TEXTURE_3D         3
#define PIPE_TEXTURE_CUBE       4
#define PIPE_TEXTURE_RECT       5

/* Virgl formats (Gallium pipe_format subset) */
#define VIRGL_FORMAT_B8G8R8A8_UNORM     1
#define VIRGL_FORMAT_B8G8R8X8_UNORM     2
#define VIRGL_FORMAT_R8G8B8A8_UNORM     67
#define VIRGL_FORMAT_R8G8B8X8_UNORM     68
#define VIRGL_FORMAT_R32G32B32A32_FLOAT  31
#define VIRGL_FORMAT_R32G32B32_FLOAT     30

/* Virgl bind flags */
#define VIRGL_BIND_DEPTH_STENCIL        (1 << 0)
#define VIRGL_BIND_RENDER_TARGET        (1 << 1)
#define VIRGL_BIND_SAMPLER_VIEW         (1 << 3)
#define VIRGL_BIND_VERTEX_BUFFER        (1 << 4)
#define VIRGL_BIND_INDEX_BUFFER         (1 << 5)
#define VIRGL_BIND_CONSTANT_BUFFER      (1 << 6)

/* Virgl capset IDs */
#define VIRTIO_GPU_CAPSET_VIRGL         1
#define VIRTIO_GPU_CAPSET_VIRGL2        2

/* ═══ Virgl command opcodes (Gallium command stream) ══════════ */

/* Each virgl command is: header dword (opcode + obj_type + length)
   followed by length dwords of payload. */

#define VIRGL_CMD_HEADER(opcode, obj_type, length) \
    ((opcode) | ((obj_type) << 8) | ((length) << 16))

/* Object types (bits 8-15 of header) */
#define VIRGL_OBJECT_BLEND              1
#define VIRGL_OBJECT_RASTERIZER         2
#define VIRGL_OBJECT_DSA                3
#define VIRGL_OBJECT_SHADER             4
#define VIRGL_OBJECT_VERTEX_ELEMENTS    5
#define VIRGL_OBJECT_SAMPLER_VIEW       6
#define VIRGL_OBJECT_SAMPLER_STATE      7
#define VIRGL_OBJECT_SURFACE            8
#define VIRGL_OBJECT_STREAMOUT_TARGET   9

/* Command opcodes (bits 0-7 of header) */
#define VIRGL_CCMD_NOP                  0
#define VIRGL_CCMD_CREATE_OBJECT        1
#define VIRGL_CCMD_BIND_OBJECT          2
#define VIRGL_CCMD_DESTROY_OBJECT       3
#define VIRGL_CCMD_SET_VIEWPORT_STATE   4
#define VIRGL_CCMD_SET_FRAMEBUFFER_STATE 5
#define VIRGL_CCMD_SET_VERTEX_BUFFERS   6
#define VIRGL_CCMD_CLEAR                7
#define VIRGL_CCMD_DRAW_VBO             8
#define VIRGL_CCMD_RESOURCE_INLINE_WRITE 9
#define VIRGL_CCMD_SET_SAMPLER_VIEWS    10
#define VIRGL_CCMD_SET_INDEX_BUFFER     11
#define VIRGL_CCMD_SET_CONSTANT_BUFFER  12
#define VIRGL_CCMD_SET_STENCIL_REF      13
#define VIRGL_CCMD_SET_BLEND_COLOR      14
#define VIRGL_CCMD_SET_SCISSOR_STATE    15
#define VIRGL_CCMD_BLIT                 16
#define VIRGL_CCMD_RESOURCE_COPY_REGION 17
#define VIRGL_CCMD_BIND_SAMPLER_STATES  18
#define VIRGL_CCMD_BEGIN_QUERY          19
#define VIRGL_CCMD_END_QUERY            20
#define VIRGL_CCMD_GET_QUERY_RESULT     21
#define VIRGL_CCMD_SET_POLYGON_STIPPLE  22
#define VIRGL_CCMD_SET_CLIP_STATE       23
#define VIRGL_CCMD_SET_SAMPLE_MASK      24
#define VIRGL_CCMD_SET_STREAMOUT_TARGETS 25
#define VIRGL_CCMD_SET_RENDER_CONDITION 26
#define VIRGL_CCMD_SET_UNIFORM_BUFFER   27
#define VIRGL_CCMD_SET_SUB_CTX          28
#define VIRGL_CCMD_CREATE_SUB_CTX       29
#define VIRGL_CCMD_DESTROY_SUB_CTX      30
#define VIRGL_CCMD_BIND_SHADER          31

/* Clear buffer bits */
#define PIPE_CLEAR_DEPTH     (1 << 0)
#define PIPE_CLEAR_STENCIL   (1 << 1)
#define PIPE_CLEAR_COLOR0    (1 << 2)
#define PIPE_CLEAR_COLOR1    (1 << 3)
#define PIPE_CLEAR_COLOR2    (1 << 4)
#define PIPE_CLEAR_COLOR3    (1 << 5)

/* Primitive types */
#define PIPE_PRIM_POINTS         0
#define PIPE_PRIM_LINES          1
#define PIPE_PRIM_LINE_STRIP     3
#define PIPE_PRIM_TRIANGLES      4
#define PIPE_PRIM_TRIANGLE_STRIP 5
#define PIPE_PRIM_TRIANGLE_FAN   6

/* Shader types */
#define PIPE_SHADER_VERTEX      0
#define PIPE_SHADER_FRAGMENT    1
#define PIPE_SHADER_GEOMETRY    2

/* TGSI token types for simple shaders */
#define TGSI_TOKEN_TYPE_DECLARATION   0
#define TGSI_TOKEN_TYPE_IMMEDIATE     1
#define TGSI_TOKEN_TYPE_INSTRUCTION   2
#define TGSI_TOKEN_TYPE_PROPERTY      3

/* ═══ Blend factors and equations ═════════════════════════════ */
/* Values from gallium/include/pipe/p_defines.h                  */

#define PIPE_BLENDFACTOR_ONE             0x01
#define PIPE_BLENDFACTOR_SRC_COLOR       0x02
#define PIPE_BLENDFACTOR_SRC_ALPHA       0x03
#define PIPE_BLENDFACTOR_DST_ALPHA       0x04
#define PIPE_BLENDFACTOR_DST_COLOR       0x05
#define PIPE_BLENDFACTOR_ZERO            0x11
#define PIPE_BLENDFACTOR_INV_SRC_COLOR   0x12
#define PIPE_BLENDFACTOR_INV_SRC_ALPHA   0x13
#define PIPE_BLENDFACTOR_INV_DST_ALPHA   0x14
#define PIPE_BLENDFACTOR_INV_DST_COLOR   0x15

#define PIPE_BLEND_ADD                   0
#define PIPE_BLEND_SUBTRACT              1
#define PIPE_BLEND_REVERSE_SUBTRACT      2
#define PIPE_BLEND_MIN                   3
#define PIPE_BLEND_MAX                   4

/* ═══ Sampler state ═══════════════════════════════════════════ */

#define PIPE_TEX_WRAP_CLAMP_TO_EDGE  2
#define PIPE_TEX_FILTER_NEAREST      0
#define PIPE_TEX_FILTER_LINEAR       1
#define PIPE_TEX_MIPFILTER_NONE      0

/* ═══ Additional virgl format ═════════════════════════════════ */

#define VIRGL_FORMAT_R32G32_FLOAT    29
#define VIRGL_FORMAT_R8_UNORM        64

#endif /* _KERNEL_VIRTIO_GPU_INTERNAL_H */
