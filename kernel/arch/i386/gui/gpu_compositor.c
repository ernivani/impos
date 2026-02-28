#include <kernel/gpu_compositor.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/virtio_gpu.h>
#include <kernel/virtio_gpu_3d.h>
#include <kernel/virtio_gpu_internal.h>
#include <kernel/ui_theme.h>
#include <kernel/pmm.h>
#include <kernel/io.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ═══ Configuration ═══════════════════════════════════════════ */

#define GPU_CTX_ID        2       /* separate from DRM's ctx_id=1 */
#define CMD_BUF_DWORDS    4096    /* 16KB command buffer */
#define MAX_GPU_SURFACES  64
#define MAX_QUADS         64      /* max surfaces drawn per frame */
#define VERT_SIZE_BYTES   16      /* float2 pos + float2 uv = 16 bytes */
#define VERTS_PER_QUAD    6       /* 2 triangles */
#define VB_MAX_BYTES      (MAX_QUADS * VERTS_PER_QUAD * VERT_SIZE_BYTES)

/* ═══ Virgl object handles ═══════════════════════════════════ */

/* We use handle IDs that won't conflict with virgl_test (ctx_id=1).
   Handle space is per-context, so any IDs are safe here. */
#define H_BLEND           1
#define H_RASTERIZER      2
#define H_DSA             3
#define H_VS              4
#define H_FS              5
#define H_VE              6
#define H_SAMPLER         7
#define H_RT_SURFACE      8
/* Surface sampler views start at 100 */
#define H_SAMPLER_VIEW_BASE 100

/* ═══ Per-surface GPU state ═══════════════════════════════════ */

typedef struct {
    int      active;
    uint32_t res_id;
    uint32_t phys;        /* PMM backing address */
    uint32_t frames;      /* PMM frame count */
    uint32_t sv_handle;   /* sampler view handle */
    int      w, h;
} gpu_surf_t;

/* ═══ Static state ═══════════════════════════════════════════ */

static int       gpu_active = 0;
static uint32_t  screen_w, screen_h;

/* Command buffer */
static uint32_t  cmd_buf[CMD_BUF_DWORDS] __attribute__((aligned(4096)));
static uint32_t  cmd_pos;

/* Render target */
static uint32_t  rt_res_id;
static uint32_t  rt_phys;
static uint32_t  rt_frames;

/* Vertex buffer */
static uint32_t  vb_res_id;
static uint32_t  vb_phys;
static uint32_t  vb_frames;

/* Per-surface tracking */
static gpu_surf_t gpu_surfs[MAX_GPU_SURFACES];

/* ═══ Command buffer helpers ═════════════════════════════════ */

static void cmd_reset(void) { cmd_pos = 0; }

static void cmd_dword(uint32_t val) {
    if (cmd_pos < CMD_BUF_DWORDS)
        cmd_buf[cmd_pos++] = val;
}

static void cmd_float(float val) {
    union { float f; uint32_t u; } c;
    c.f = val;
    cmd_dword(c.u);
}

static int cmd_submit(void) {
    if (cmd_pos == 0) return 0;
    return virtio_gpu_3d_submit(GPU_CTX_ID, cmd_buf, cmd_pos * 4);
}

/* ═══ Gallium command encoders ═══════════════════════════════ */

static void encode_create_surface(uint32_t handle, uint32_t res_id,
                                   uint32_t format, uint32_t first, uint32_t last) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE, 5));
    cmd_dword(handle);
    cmd_dword(res_id);
    cmd_dword(format);
    cmd_dword(first);
    cmd_dword(last);
}

static void encode_set_framebuffer(uint32_t nr_cbufs, uint32_t zsurf,
                                    uint32_t *cbufs) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, 2 + nr_cbufs));
    cmd_dword(nr_cbufs);
    cmd_dword(zsurf);
    for (uint32_t i = 0; i < nr_cbufs; i++)
        cmd_dword(cbufs[i]);
}

static void encode_clear(uint32_t buffers,
                           float r, float g, float b, float a) {
    union { double d; uint32_t u[2]; } dc;
    dc.d = 1.0;
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CLEAR, 0, 8));
    cmd_dword(buffers);
    cmd_float(r); cmd_float(g); cmd_float(b); cmd_float(a);
    cmd_dword(dc.u[0]); cmd_dword(dc.u[1]);
    cmd_dword(0);
}

static void encode_set_viewport(float w, float h) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_SET_VIEWPORT_STATE, 0, 7));
    cmd_dword(0);            /* start_slot */
    cmd_float(w / 2.0f);    /* scale_x */
    cmd_float(h / 2.0f);    /* scale_y */
    cmd_float(0.5f);         /* scale_z */
    cmd_float(w / 2.0f);    /* translate_x */
    cmd_float(h / 2.0f);    /* translate_y */
    cmd_float(0.5f);         /* translate_z */
}

static void encode_create_blend_alpha(uint32_t handle) {
    /* Alpha blending: src_alpha / inv_src_alpha for compositing.
       Each RT is 2 dwords: [0] = packed blend state, [1] = srgb_write.
       Total payload: handle(1) + S0(1) + S1(1) + RT[0..3](8) = 11 dwords.
       Bit layout of RT dword 0:
         [0]     blend_enable
         [1:3]   rgb_func        (3 bits)
         [4:8]   rgb_src_factor  (5 bits)
         [9:13]  rgb_dst_factor  (5 bits)
         [14:16] alpha_func      (3 bits)
         [17:21] alpha_src_factor(5 bits)
         [22:26] alpha_dst_factor(5 bits)
         [27:30] colormask       (4 bits: R G B A) */
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_BLEND, 11));
    cmd_dword(handle);
    cmd_dword(0);  /* S0: no logicop, no dither */
    cmd_dword(0);  /* S1: independent_blend=0 */
    /* RT[0]: src_alpha blending with RGBA colormask */
    uint32_t rt0 = (1u << 0)                              /* blend_enable = 1 */
                 | ((uint32_t)PIPE_BLEND_ADD << 1)         /* rgb_func = ADD */
                 | ((uint32_t)PIPE_BLENDFACTOR_SRC_ALPHA << 4)    /* rgb_src = SRC_ALPHA */
                 | ((uint32_t)PIPE_BLENDFACTOR_INV_SRC_ALPHA << 9)/* rgb_dst = INV_SRC_ALPHA */
                 | ((uint32_t)PIPE_BLEND_ADD << 14)        /* alpha_func = ADD */
                 | ((uint32_t)PIPE_BLENDFACTOR_ONE << 17)  /* alpha_src = ONE */
                 | ((uint32_t)PIPE_BLENDFACTOR_INV_SRC_ALPHA << 22)/* alpha_dst = INV_SRC_ALPHA */
                 | (0xFu << 27);                           /* colormask = RGBA */
    cmd_dword(rt0);
    cmd_dword(0);  /* RT[0] dword 1: srgb_write = 0 */
    cmd_dword(0);  /* RT[1] dword 0 */
    cmd_dword(0);  /* RT[1] dword 1 */
    cmd_dword(0);  /* RT[2] dword 0 */
    cmd_dword(0);  /* RT[2] dword 1 */
    cmd_dword(0);  /* RT[3] dword 0 */
    cmd_dword(0);  /* RT[3] dword 1 */
}

static void encode_create_rasterizer(uint32_t handle) {
    /* Field ordering from virgl_protocol.h VIRGL_OBJ_RS_* macros:
         pos 1: handle
         pos 2: S0 (packed control bits)
         pos 3: point_size (float)
         pos 4: sprite_coord_enable
         pos 5: S3 (packed: line_stipple_pattern[15:0] | factor[23:16] | clip_plane[31:24])
         pos 6: line_width (float)
         pos 7: offset_units (float)
         pos 8: offset_scale (float)
         pos 9: offset_clamp (float)  */
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_RASTERIZER, 9));
    cmd_dword(handle);
    uint32_t s0 = (1u << 1)     /* depth_clip */
               | (1u << 15)    /* front_ccw (OpenGL convention) */
               | (1u << 29);   /* half_pixel_center */
    cmd_dword(s0);              /* pos 2: S0 */
    cmd_float(1.0f);            /* pos 3: point_size */
    cmd_dword(0);               /* pos 4: sprite_coord_enable */
    cmd_dword(0);               /* pos 5: S3 (no stipple, no clip planes) */
    cmd_float(0.0f);            /* pos 6: line_width */
    cmd_float(0.0f);            /* pos 7: offset_units */
    cmd_float(0.0f);            /* pos 8: offset_scale */
    cmd_float(0.0f);            /* pos 9: offset_clamp */
}

static void encode_create_dsa(uint32_t handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_DSA, 5));
    cmd_dword(handle);
    cmd_dword(0);  /* no depth test */
    cmd_dword(0);  /* no stencil front */
    cmd_dword(0);  /* no stencil back */
    cmd_dword(0);  /* no alpha test */
}

static void encode_bind_object(uint32_t handle, uint32_t obj_type) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_OBJECT, obj_type, 1));
    cmd_dword(handle);
}

/* Vertex elements: 2 attrs (float2 position + float2 texcoord), stride=16 */
static void encode_create_ve_2d(uint32_t handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                VIRGL_OBJECT_VERTEX_ELEMENTS, 1 + 2 * 4));
    cmd_dword(handle);
    /* Element 0: position float2 at offset 0 */
    cmd_dword(0);                              /* src_offset = 0 */
    cmd_dword(0);                              /* instance_divisor */
    cmd_dword(0);                              /* vertex_buffer_index */
    cmd_dword(VIRGL_FORMAT_R32G32_FLOAT);      /* src_format = vec2 */
    /* Element 1: texcoord float2 at offset 8 */
    cmd_dword(8);                              /* src_offset = 8 */
    cmd_dword(0);                              /* instance_divisor */
    cmd_dword(0);                              /* vertex_buffer_index */
    cmd_dword(VIRGL_FORMAT_R32G32_FLOAT);      /* src_format = vec2 */
}

static void encode_set_vertex_buffers(uint32_t stride, uint32_t offset,
                                       uint32_t res_handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_SET_VERTEX_BUFFERS, 0, 3));
    cmd_dword(stride);
    cmd_dword(offset);
    cmd_dword(res_handle);
}

static void encode_draw_vbo(uint32_t mode, uint32_t start, uint32_t count) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_DRAW_VBO, 0, 12));
    cmd_dword(start);
    cmd_dword(count);
    cmd_dword(mode);
    cmd_dword(0);             /* indexed */
    cmd_dword(1);             /* instance_count */
    cmd_dword(0);             /* index_bias */
    cmd_dword(0);             /* start_instance */
    cmd_dword(0);             /* primitive_restart */
    cmd_dword(0);             /* restart_index */
    cmd_dword(0);             /* min_index */
    cmd_dword(count - 1);     /* max_index */
    cmd_dword(0);             /* cso */
}

/* ═══ Shader encoders ════════════════════════════════════════ */

/* VIRGL_CCMD_CREATE_OBJECT / VIRGL_OBJECT_SHADER (TGSI text mode)
   Payload layout (positions 1-based):
     1: handle
     2: type (PIPE_SHADER_VERTEX / PIPE_SHADER_FRAGMENT)
     3: offlen (byte length of TGSI text including NUL)
     4: num_tokens (buffer allocation hint for tgsi_text_translate)
     5: num_so_outputs (stream output count, 0 for no SO)
     6+: TGSI text packed into dwords (NUL-padded)
   num_tokens is NOT 0 — virglrenderer allocates calloc(num_tokens+10)
   token slots for the text parser. 300 provides ample headroom. */
static void encode_create_shader(uint32_t handle, uint32_t type,
                                   const char *tgsi_text) {
    uint32_t text_len = (uint32_t)strlen(tgsi_text) + 1;  /* include NUL */
    uint32_t text_dwords = (text_len + 3) / 4;
    uint32_t payload_len = 5 + text_dwords;

    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                VIRGL_OBJECT_SHADER, payload_len));
    cmd_dword(handle);
    cmd_dword(type);
    cmd_dword(text_len);        /* offlen = byte length of text (text mode) */
    cmd_dword(300);             /* num_tokens = buffer size hint (NOT 0!) */
    cmd_dword(0);               /* num_so_outputs = 0 */

    /* Pack TGSI text into dwords, NUL-padded to 4-byte boundary */
    const uint8_t *src = (const uint8_t *)tgsi_text;
    for (uint32_t i = 0; i < text_dwords; i++) {
        uint32_t d = 0;
        for (int b = 0; b < 4; b++) {
            uint32_t off = i * 4 + (uint32_t)b;
            if (off < text_len)
                d |= (uint32_t)src[off] << (b * 8);
        }
        cmd_dword(d);
    }
}

static void encode_bind_shader(uint32_t handle, uint32_t type) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_SHADER, 0, 2));
    cmd_dword(handle);
    cmd_dword(type);
}

/* ═══ Sampler state and view encoders ════════════════════════ */

/* Create sampler state (per-context, binds to fragment stage).
   virgl sampler state = 9 dwords:
     handle, S0(packed filters), lod_bias, min_lod, max_lod,
     border_color[0..3] */
static void encode_create_sampler_state(uint32_t handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                VIRGL_OBJECT_SAMPLER_STATE, 9));
    cmd_dword(handle);
    /* Pack: wrap_s(3) | wrap_t(3) | wrap_r(3) | min_img(2) | min_mip(2) | mag(2)
       bits: wrap_s[0:2] wrap_t[3:5] wrap_r[6:8] min_img[9:10] min_mip[11:12] mag[13:14]
       Using nearest filter, clamp-to-edge wrap: */
    uint32_t s0 = (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 0)  |
                  (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 3)  |
                  (PIPE_TEX_WRAP_CLAMP_TO_EDGE << 6)  |
                  (PIPE_TEX_FILTER_NEAREST << 9)       |
                  (PIPE_TEX_MIPFILTER_NONE << 11)      |
                  (PIPE_TEX_FILTER_NEAREST << 13);
    cmd_dword(s0);
    cmd_float(0.0f);   /* lod_bias */
    cmd_float(0.0f);   /* min_lod */
    cmd_float(0.0f);   /* max_lod */
    cmd_dword(0);      /* border_color[0] (R) */
    cmd_dword(0);      /* border_color[1] (G) */
    cmd_dword(0);      /* border_color[2] (B) */
    cmd_dword(0);      /* border_color[3] (A) */
}

/* Create a sampler view for a texture resource.
   virgl sampler_view = 6 dwords:
     handle, res_id, format, val0(first_element/level), val1(last_element/level),
     swizzle_packed */
static void encode_create_sampler_view(uint32_t handle, uint32_t res_id,
                                        uint32_t format) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                VIRGL_OBJECT_SAMPLER_VIEW, 6));
    cmd_dword(handle);
    cmd_dword(res_id);
    cmd_dword(format);
    cmd_dword(0);    /* val0: first_level = 0 */
    cmd_dword(0);    /* val1: last_level = 0 */
    /* Swizzle: RGBA identity = 0,1,2,3 packed as 3 bits each */
    cmd_dword((0) | (1 << 3) | (2 << 6) | (3 << 9));
}

/* SET_SAMPLER_VIEWS: bind sampler views to a shader stage.
   v0.9.1 protocol: shader_type and start_slot are separate dwords.
   Payload: shader_type, start_slot, view_handles... */
static void encode_set_sampler_views(uint32_t shader_type, uint32_t sv_handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_SET_SAMPLER_VIEWS, 0, 3));
    cmd_dword(shader_type);
    cmd_dword(0);            /* start_slot = 0 */
    cmd_dword(sv_handle);
}

/* BIND_SAMPLER_STATES: bind sampler state objects to a shader stage.
   v0.9.1 protocol: shader_type and start_slot are separate dwords.
   Payload: shader_type, start_slot, sampler_handles... */
static void encode_bind_sampler_states(uint32_t shader_type, uint32_t handle) {
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_SAMPLER_STATES, 0, 3));
    cmd_dword(shader_type);
    cmd_dword(0);            /* start_slot = 0 */
    cmd_dword(handle);
}

/* ═══ TGSI text shaders ══════════════════════════════════════ */

/* Vertex shader: float2 position (IN[0]) + float2 texcoord (IN[1]).
   OpenGL expands vec2 inputs to vec4 as (x, y, 0, 1).
   Output: POSITION = (pos.x, pos.y, 0, 1), GENERIC[0] = (uv.x, uv.y, 0, 1). */
static const char tgsi_vs[] =
    "VERT\n"
    "DCL IN[0]\n"
    "DCL IN[1]\n"
    "DCL OUT[0], POSITION\n"
    "DCL OUT[1], GENERIC[0]\n"
    "  0: MOV OUT[0], IN[0]\n"
    "  1: MOV OUT[1], IN[1]\n"
    "  2: END\n";

/* Fragment shader: sample 2D texture at interpolated texcoord.
   IN[0] GENERIC[0] = texcoord from VS, linearly interpolated. */
static const char tgsi_fs[] =
    "FRAG\n"
    "DCL IN[0], GENERIC[0], LINEAR\n"
    "DCL OUT[0], COLOR\n"
    "DCL SAMP[0]\n"
    "DCL SVIEW[0], 2D, FLOAT\n"
    "  0: TEX OUT[0], IN[0], SAMP[0], 2D\n"
    "  1: END\n";

/* ═══ Resource helpers ═══════════════════════════════════════ */

/* Allocate a 3D texture resource with PMM backing.  Returns 0 on failure. */
static int alloc_3d_resource(uint32_t *out_res, uint32_t *out_phys,
                              uint32_t *out_frames,
                              uint32_t target, uint32_t format, uint32_t bind,
                              uint32_t w, uint32_t h) {
    uint32_t res_id = virtio_gpu_alloc_resource_id();
    uint32_t size = w * h * 4;
    uint32_t nframes = (size + 4095) / 4096;
    if (nframes == 0) nframes = 1;

    if (virtio_gpu_3d_resource_create(GPU_CTX_ID, res_id,
                                       target, format, bind,
                                       w, h, 1, 1, 0, 0, 0) != 0) {
        DBG("GPU_COMP: resource_create_3d failed (res=%u %ux%u)", res_id, w, h);
        return 0;
    }

    uint32_t phys = pmm_alloc_contiguous(nframes);
    if (!phys) {
        DBG("GPU_COMP: PMM alloc failed (%u frames)", nframes);
        return 0;
    }
    memset((void *)phys, 0, nframes * 4096);

    if (virtio_gpu_attach_resource_backing(res_id, (uint32_t *)phys, size) != 0) {
        DBG("GPU_COMP: attach_backing failed (res=%u)", res_id);
        pmm_free_contiguous(phys, nframes);
        return 0;
    }

    if (virtio_gpu_3d_ctx_attach_resource(GPU_CTX_ID, res_id) != 0) {
        DBG("GPU_COMP: ctx_attach_resource failed (res=%u)", res_id);
        pmm_free_contiguous(phys, nframes);
        return 0;
    }

    *out_res = res_id;
    *out_phys = phys;
    *out_frames = nframes;
    return 1;
}

static void free_3d_resource(uint32_t res_id, uint32_t phys, uint32_t frames) {
    virtio_gpu_3d_ctx_detach_resource(GPU_CTX_ID, res_id);
    pmm_free_contiguous(phys, frames);
}

/* ═══ Initialization ═════════════════════════════════════════ */

int gpu_comp_init(void) {
    gpu_active = 0;

    if (!virtio_gpu_is_active() || !virtio_gpu_has_virgl()) {
        DBG("COMP: GPU compositor requires virgl, falling back to software");
        return 0;
    }

    screen_w = gfx_width();
    screen_h = gfx_height();

    /* Create a dedicated virgl context */
    if (virtio_gpu_3d_ctx_create(GPU_CTX_ID, "gpu-comp") != 0) {
        DBG("COMP: GPU compositor ctx_create failed");
        return 0;
    }

    /* Allocate render target (full-screen BGRA texture) */
    if (!alloc_3d_resource(&rt_res_id, &rt_phys, &rt_frames,
                            PIPE_TEXTURE_2D,
                            VIRGL_FORMAT_B8G8R8A8_UNORM,
                            VIRGL_BIND_RENDER_TARGET | VIRGL_BIND_SAMPLER_VIEW,
                            screen_w, screen_h)) {
        DBG("COMP: GPU compositor render target alloc failed");
        virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
        return 0;
    }

    /* Allocate vertex buffer (PIPE_BUFFER) */
    /* For PIPE_BUFFER: width = byte size, height = 1, depth = 1 */
    uint32_t vb_size = VB_MAX_BYTES;
    vb_res_id = virtio_gpu_alloc_resource_id();
    if (virtio_gpu_3d_resource_create(GPU_CTX_ID, vb_res_id,
                                       PIPE_BUFFER, VIRGL_FORMAT_R8_UNORM,
                                       VIRGL_BIND_VERTEX_BUFFER,
                                       vb_size, 1, 1, 1, 0, 0, 0) != 0) {
        DBG("COMP: GPU compositor VB resource_create failed");
        free_3d_resource(rt_res_id, rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
        return 0;
    }

    vb_frames = (vb_size + 4095) / 4096;
    vb_phys = pmm_alloc_contiguous(vb_frames);
    if (!vb_phys) {
        DBG("COMP: GPU compositor VB PMM alloc failed");
        free_3d_resource(rt_res_id, rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
        return 0;
    }
    memset((void *)vb_phys, 0, vb_frames * 4096);

    if (virtio_gpu_attach_resource_backing(vb_res_id, (uint32_t *)vb_phys, vb_size) != 0 ||
        virtio_gpu_3d_ctx_attach_resource(GPU_CTX_ID, vb_res_id) != 0) {
        DBG("COMP: GPU compositor VB attach failed");
        pmm_free_contiguous(vb_phys, vb_frames);
        free_3d_resource(rt_res_id, rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
        return 0;
    }

    /* Zero out surface tracking */
    memset(gpu_surfs, 0, sizeof(gpu_surfs));

    /* ── Submit pipeline setup batch ───────────────────────── */
    cmd_reset();

    /* Create and bind blend state (alpha blending) */
    encode_create_blend_alpha(H_BLEND);
    encode_bind_object(H_BLEND, VIRGL_OBJECT_BLEND);

    /* Create and bind rasterizer (no culling) */
    encode_create_rasterizer(H_RASTERIZER);
    encode_bind_object(H_RASTERIZER, VIRGL_OBJECT_RASTERIZER);

    /* Create and bind DSA (no depth test) */
    encode_create_dsa(H_DSA);
    encode_bind_object(H_DSA, VIRGL_OBJECT_DSA);

    /* Create and bind vertex elements (float2 pos + float2 uv) */
    encode_create_ve_2d(H_VE);
    encode_bind_object(H_VE, VIRGL_OBJECT_VERTEX_ELEMENTS);

    /* Create and bind shaders (TGSI text, num_tokens=300 buffer hint) */
    encode_create_shader(H_VS, PIPE_SHADER_VERTEX, tgsi_vs);
    encode_bind_shader(H_VS, PIPE_SHADER_VERTEX);

    encode_create_shader(H_FS, PIPE_SHADER_FRAGMENT, tgsi_fs);
    encode_bind_shader(H_FS, PIPE_SHADER_FRAGMENT);

    /* Create sampler state (nearest filter, clamp) */
    encode_create_sampler_state(H_SAMPLER);
    encode_bind_sampler_states(PIPE_SHADER_FRAGMENT, H_SAMPLER);

    /* Create surface object for render target */
    encode_create_surface(H_RT_SURFACE, rt_res_id,
                           VIRGL_FORMAT_B8G8R8A8_UNORM, 0, 0);

    if (cmd_submit() != 0) {
        DBG("COMP: GPU compositor pipeline setup submit failed");
        pmm_free_contiguous(vb_phys, vb_frames);
        free_3d_resource(rt_res_id, rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
        return 0;
    }

    /* ── Self-test: clear + readback to verify RT pipeline ── */
    {
        cmd_reset();
        uint32_t cbuf = H_RT_SURFACE;
        encode_set_framebuffer(1, 0, &cbuf);
        encode_set_viewport((float)screen_w, (float)screen_h);
        encode_clear(PIPE_CLEAR_COLOR0, 0.0f, 0.0f, 1.0f, 1.0f); /* blue */

        int ret = cmd_submit();

        /* Readback and verify clear color */
        struct virtio_gpu_box box;
        box.x = 0; box.y = 0; box.z = 0;
        box.w = screen_w; box.h = screen_h; box.d = 1;
        virtio_gpu_3d_transfer_from_host(rt_res_id, GPU_CTX_ID,
                                          0, screen_w * 4, 0, &box, 0);

        uint32_t *rt = (uint32_t *)rt_phys;
        uint32_t px0 = rt[0];
        /* Blue clear in B8G8R8A8: B=0xFF, G=0, R=0, A=0xFF → 0xFF0000FF */
        if (ret != 0 || (px0 != 0xFF0000FF && px0 != 0xFFFF0000)) {
            DBG("GPU_COMP: self-test FAILED (ret=%d px0=%x)", ret, px0);
            pmm_free_contiguous(vb_phys, vb_frames);
            free_3d_resource(rt_res_id, rt_phys, rt_frames);
            virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
            return 0;
        }
        DBG("GPU_COMP: self-test OK (clear=%x)", px0);
    }

    gpu_active = 1;
    DBG("COMP: GPU-accelerated compositor active (%ux%u)", screen_w, screen_h);
    return 1;
}

void gpu_comp_shutdown(void) {
    if (!gpu_active) return;

    /* Destroy per-surface resources */
    for (int i = 0; i < MAX_GPU_SURFACES; i++) {
        if (gpu_surfs[i].active) {
            free_3d_resource(gpu_surfs[i].res_id,
                              gpu_surfs[i].phys, gpu_surfs[i].frames);
            gpu_surfs[i].active = 0;
        }
    }

    /* Destroy VB and RT */
    virtio_gpu_3d_ctx_detach_resource(GPU_CTX_ID, vb_res_id);
    pmm_free_contiguous(vb_phys, vb_frames);
    free_3d_resource(rt_res_id, rt_phys, rt_frames);

    virtio_gpu_3d_ctx_destroy(GPU_CTX_ID);
    gpu_active = 0;
    DBG("COMP: GPU compositor shut down");
}

int gpu_comp_is_active(void) { return gpu_active; }

/* ═══ Per-surface management ═════════════════════════════════ */

void gpu_comp_surface_created(int pool_idx, int w, int h) {
    if (!gpu_active || pool_idx < 0 || pool_idx >= MAX_GPU_SURFACES) return;

    gpu_surf_t *gs = &gpu_surfs[pool_idx];
    if (gs->active) {
        /* Already tracked — destroy old first */
        free_3d_resource(gs->res_id, gs->phys, gs->frames);
    }

    gs->w = w;
    gs->h = h;
    gs->sv_handle = H_SAMPLER_VIEW_BASE + (uint32_t)pool_idx;

    if (!alloc_3d_resource(&gs->res_id, &gs->phys, &gs->frames,
                            PIPE_TEXTURE_2D,
                            VIRGL_FORMAT_B8G8R8A8_UNORM,
                            VIRGL_BIND_SAMPLER_VIEW,
                            (uint32_t)w, (uint32_t)h)) {
        gs->active = 0;
        return;
    }

    /* Create sampler view for this texture */
    cmd_reset();
    encode_create_sampler_view(gs->sv_handle, gs->res_id,
                                VIRGL_FORMAT_B8G8R8A8_UNORM);
    cmd_submit();

    /* Upload zero-initialized PMM to host so the texture isn't garbage.
       Without this initial transfer, the host GPU texture contains
       uninitialized data which produces garbled pixels when sampled. */
    {
        struct virtio_gpu_box box;
        box.x = 0; box.y = 0; box.z = 0;
        box.w = (uint32_t)w; box.h = (uint32_t)h; box.d = 1;
        virtio_gpu_3d_transfer_to_host(gs->res_id, GPU_CTX_ID,
                                        0, (uint32_t)w * 4, 0, &box, 0);
    }

    gs->active = 1;
}

void gpu_comp_surface_destroyed(int pool_idx) {
    if (!gpu_active || pool_idx < 0 || pool_idx >= MAX_GPU_SURFACES) return;

    gpu_surf_t *gs = &gpu_surfs[pool_idx];
    if (!gs->active) return;

    /* Destroy sampler view object */
    cmd_reset();
    cmd_dword(VIRGL_CMD_HEADER(VIRGL_CCMD_DESTROY_OBJECT,
                                VIRGL_OBJECT_SAMPLER_VIEW, 1));
    cmd_dword(gs->sv_handle);
    cmd_submit();

    free_3d_resource(gs->res_id, gs->phys, gs->frames);
    gs->active = 0;
}

void gpu_comp_surface_resized(int pool_idx, int new_w, int new_h) {
    if (!gpu_active) return;
    gpu_comp_surface_destroyed(pool_idx);
    gpu_comp_surface_created(pool_idx, new_w, new_h);
}

/* ═══ Render loop ════════════════════════════════════════════ */

void gpu_comp_render_frame(void) {
    if (!gpu_active) return;

    static int first_frame = 1;

    /* 1. Upload dirty surface textures to GPU */
    for (int i = 0; i < MAX_GPU_SURFACES; i++) {
        gpu_surf_t *gs = &gpu_surfs[i];
        if (!gs->active) continue;

        comp_surface_t *cs = &comp_pool[i];
        if (!cs->in_use || !cs->visible) continue;
        if (!cs->damage_all && cs->dmg_w == 0) continue;

        /* Pre-multiply alpha for surfaces with < 255 opacity.
           Copy pixel data to PMM backing, applying surface alpha. */
        uint32_t *src = cs->pixels;
        uint32_t *dst = (uint32_t *)gs->phys;
        int npix = cs->w * cs->h;

        if (cs->alpha == 255) {
            memcpy(dst, src, (size_t)npix * 4);
        } else {
            /* Pre-multiply alpha: for each pixel, modulate alpha channel */
            uint32_t sa = cs->alpha;
            for (int p = 0; p < npix; p++) {
                uint32_t px = src[p];
                uint32_t a = (px >> 24) & 0xFF;
                a = (a * sa) >> 8;
                dst[p] = (a << 24) | (px & 0x00FFFFFF);
            }
        }

        /* Transfer pixels to host GPU texture */
        struct virtio_gpu_box box;
        box.x = 0; box.y = 0; box.z = 0;
        box.w = (uint32_t)cs->w; box.h = (uint32_t)cs->h; box.d = 1;
        virtio_gpu_3d_transfer_to_host(gs->res_id, GPU_CTX_ID,
                                        0, (uint32_t)cs->w * 4, 0, &box, 0);
    }

    /* 2. Build vertex buffer with textured quads for each visible surface */
    float *vb = (float *)vb_phys;
    int quad_count = 0;

    for (int L = 0; L < COMP_LAYER_COUNT; L++) {
        for (int i = 0; i < comp_layer_count[L]; i++) {
            int idx = comp_layer_idx[L][i];
            comp_surface_t *cs = &comp_pool[idx];
            if (!cs->in_use || !cs->visible) continue;
            if (!gpu_surfs[idx].active) continue;
            if (quad_count >= MAX_QUADS) break;

            /* Convert screen coords to NDC: x -> [-1, 1], y -> [-1, 1]
               NDC origin is bottom-left with Y up, screen origin is top-left Y down */
            float x0 = (float)cs->screen_x / (float)screen_w * 2.0f - 1.0f;
            float y0 = 1.0f - (float)cs->screen_y / (float)screen_h * 2.0f;
            float x1 = (float)(cs->screen_x + cs->w) / (float)screen_w * 2.0f - 1.0f;
            float y1 = 1.0f - (float)(cs->screen_y + cs->h) / (float)screen_h * 2.0f;

            /* UV coords: (0,0) top-left, (1,1) bottom-right */
            float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

            /* 6 vertices = 2 triangles (CCW winding):
               Triangle 1: TL, BL, BR
               Triangle 2: TL, BR, TR */
            int base = quad_count * VERTS_PER_QUAD * 4;  /* 4 floats per vertex */

            /* Triangle 1: top-left */
            vb[base + 0] = x0; vb[base + 1] = y0; vb[base + 2] = u0; vb[base + 3] = v0;
            /* bottom-left */
            vb[base + 4] = x0; vb[base + 5] = y1; vb[base + 6] = u0; vb[base + 7] = v1;
            /* bottom-right */
            vb[base + 8] = x1; vb[base + 9] = y1; vb[base + 10] = u1; vb[base + 11] = v1;

            /* Triangle 2: top-left */
            vb[base + 12] = x0; vb[base + 13] = y0; vb[base + 14] = u0; vb[base + 15] = v0;
            /* bottom-right */
            vb[base + 16] = x1; vb[base + 17] = y1; vb[base + 18] = u1; vb[base + 19] = v1;
            /* top-right */
            vb[base + 20] = x1; vb[base + 21] = y0; vb[base + 22] = u1; vb[base + 23] = v0;

            quad_count++;
        }
    }

    if (quad_count == 0) {
        if (first_frame) {
            DBG("GPU_COMP: first frame — no visible quads");
            first_frame = 0;
        }
        return;
    }

    if (first_frame) {
        DBG("GPU_COMP: first frame — %d quads", quad_count);
    }

    /* Upload vertex buffer to host */
    {
        uint32_t vb_byte_size = (uint32_t)quad_count * VERTS_PER_QUAD * VERT_SIZE_BYTES;
        struct virtio_gpu_box box;
        box.x = 0; box.y = 0; box.z = 0;
        box.w = vb_byte_size; box.h = 1; box.d = 1;
        virtio_gpu_3d_transfer_to_host(vb_res_id, GPU_CTX_ID,
                                        0, 0, 0, &box, 0);
    }

    /* 3. Encode render commands */
    cmd_reset();

    /* Set framebuffer to render target */
    uint32_t cbuf = H_RT_SURFACE;
    encode_set_framebuffer(1, 0, &cbuf);

    /* Set viewport to full screen */
    encode_set_viewport((float)screen_w, (float)screen_h);

    /* Clear to desktop background color */
    {
        uint32_t bg = ui_theme.desktop_bg;
        float cr = (float)((bg >> 16) & 0xFF) / 255.0f;
        float cg = (float)((bg >> 8) & 0xFF) / 255.0f;
        float cb_val = (float)(bg & 0xFF) / 255.0f;
        encode_clear(PIPE_CLEAR_COLOR0, cr, cg, cb_val, 1.0f);
    }

    /* Re-bind pipeline state (may not persist across submit boundaries) */
    encode_bind_object(H_BLEND, VIRGL_OBJECT_BLEND);
    encode_bind_object(H_RASTERIZER, VIRGL_OBJECT_RASTERIZER);
    encode_bind_object(H_DSA, VIRGL_OBJECT_DSA);
    encode_bind_object(H_VE, VIRGL_OBJECT_VERTEX_ELEMENTS);
    encode_bind_shader(H_VS, PIPE_SHADER_VERTEX);
    encode_bind_shader(H_FS, PIPE_SHADER_FRAGMENT);
    encode_bind_sampler_states(PIPE_SHADER_FRAGMENT, H_SAMPLER);

    /* Bind vertex buffer (shared across all draws) */
    encode_set_vertex_buffers(VERT_SIZE_BYTES, 0, vb_res_id);

    /* Draw each visible surface as a textured quad */
    int quad_idx = 0;
    for (int L = 0; L < COMP_LAYER_COUNT; L++) {
        for (int i = 0; i < comp_layer_count[L]; i++) {
            int idx = comp_layer_idx[L][i];
            comp_surface_t *cs = &comp_pool[idx];
            if (!cs->in_use || !cs->visible) continue;
            if (!gpu_surfs[idx].active) continue;
            if (quad_idx >= MAX_QUADS) break;

            /* Bind this surface's texture for sampling */
            encode_set_sampler_views(PIPE_SHADER_FRAGMENT,
                                      gpu_surfs[idx].sv_handle);

            /* Draw 6 vertices for this quad */
            uint32_t start_vert = (uint32_t)quad_idx * VERTS_PER_QUAD;
            encode_draw_vbo(PIPE_PRIM_TRIANGLES, start_vert, VERTS_PER_QUAD);

            quad_idx++;
        }
    }

    /* 4. Submit command stream to GPU */
    int submit_ret = cmd_submit();
    if (submit_ret != 0) {
        DBG("GPU_COMP: frame submit failed");
        return;
    }

    /* 5. Readback rendered pixels from GPU */
    {
        struct virtio_gpu_box box;
        box.x = 0; box.y = 0; box.z = 0;
        box.w = screen_w; box.h = screen_h; box.d = 1;
        virtio_gpu_3d_transfer_from_host(rt_res_id, GPU_CTX_ID,
                                          0, screen_w * 4, 0, &box, 0);
    }

    /* 6. Copy rendered frame to backbuffer and flip.
       With clip_halfz=1, OpenGL renders Y=+1 at the top of the framebuffer.
       However, glReadPixels / TRANSFER_FROM_HOST_3D returns data starting
       from the bottom row (GL convention).  We flip during copy so that
       screen row 0 (top) reads from the last row of the RT backing store. */
    uint32_t *bb = gfx_backbuffer();
    if (bb) {
        uint32_t pitch4 = gfx_pitch() / 4;
        uint32_t *rt_pixels = (uint32_t *)rt_phys;
        for (uint32_t y = 0; y < screen_h; y++) {
            memcpy(&bb[y * pitch4],
                   &rt_pixels[(screen_h - 1 - y) * screen_w],
                   screen_w * 4);
        }
        gfx_flip_rect(0, 0, (int)screen_w, (int)screen_h);

        if (first_frame)
            first_frame = 0;
    }
}
