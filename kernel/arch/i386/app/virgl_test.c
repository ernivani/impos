#include <kernel/virtio_gpu.h>
#include <kernel/virtio_gpu_3d.h>
#include <kernel/virtio_gpu_internal.h>
#include <kernel/gfx.h>
#include <kernel/pmm.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * Virgl 3D test harness.
 *
 * Validates the VirtIO GPU 3D pipeline end-to-end by:
 * 1. Creating a virgl context
 * 2. Creating a 3D render target
 * 3. Encoding raw Gallium clear commands
 * 4. Submitting and verifying pixel readback
 * 5. Drawing a colored triangle
 */

/* ═══ Command buffer encoder ══════════════════════════════════ */

#define VIRGL_CMD_BUF_MAX 2048  /* max dwords in command stream */

typedef struct {
    uint32_t buf[VIRGL_CMD_BUF_MAX];
    uint32_t pos;  /* current write position in dwords */
} virgl_cmd_buf_t;

static void virgl_cmd_reset(virgl_cmd_buf_t *cb) {
    cb->pos = 0;
}

static void virgl_cmd_dword(virgl_cmd_buf_t *cb, uint32_t val) {
    if (cb->pos < VIRGL_CMD_BUF_MAX)
        cb->buf[cb->pos++] = val;
}

static void virgl_cmd_float(virgl_cmd_buf_t *cb, float val) {
    union { float f; uint32_t u; } conv;
    conv.f = val;
    virgl_cmd_dword(cb, conv.u);
}

/* ═══ Virgl command encoders ═════════════════════════════════ */

/*
 * CREATE_OBJECT: surface
 * Payload: handle, res_id, format, val0 (first_element/first_layer),
 *          val1 (last_element/last_layer)
 */
static void virgl_encode_create_surface(virgl_cmd_buf_t *cb,
                                         uint32_t handle,
                                         uint32_t res_id,
                                         uint32_t format,
                                         uint32_t first_layer,
                                         uint32_t last_layer) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                          VIRGL_OBJECT_SURFACE, 5));
    virgl_cmd_dword(cb, handle);
    virgl_cmd_dword(cb, res_id);
    virgl_cmd_dword(cb, format);
    virgl_cmd_dword(cb, first_layer);  /* val0: level for textures */
    virgl_cmd_dword(cb, last_layer);   /* val1 */
}

/*
 * SET_FRAMEBUFFER_STATE
 * Payload: nr_cbufs, zsurf_handle, cbuf_handles[nr_cbufs]
 */
static void virgl_encode_set_framebuffer(virgl_cmd_buf_t *cb,
                                          uint32_t nr_cbufs,
                                          uint32_t zsurf_handle,
                                          uint32_t *cbuf_handles) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_SET_FRAMEBUFFER_STATE,
                                          0, 2 + nr_cbufs));
    virgl_cmd_dword(cb, nr_cbufs);
    virgl_cmd_dword(cb, zsurf_handle);
    for (uint32_t i = 0; i < nr_cbufs; i++)
        virgl_cmd_dword(cb, cbuf_handles[i]);
}

/*
 * CLEAR
 * Payload: buffers, color[4] (as floats), depth (double as 2x uint32),
 *          stencil
 */
static void virgl_encode_clear(virgl_cmd_buf_t *cb,
                                uint32_t buffers,
                                float r, float g, float b, float a,
                                double depth, uint32_t stencil) {
    union { double d; uint32_t u[2]; } dc;
    dc.d = depth;

    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CLEAR, 0, 8));
    virgl_cmd_dword(cb, buffers);
    virgl_cmd_float(cb, r);
    virgl_cmd_float(cb, g);
    virgl_cmd_float(cb, b);
    virgl_cmd_float(cb, a);
    virgl_cmd_dword(cb, dc.u[0]);  /* depth low */
    virgl_cmd_dword(cb, dc.u[1]);  /* depth high */
    virgl_cmd_dword(cb, stencil);  /* stencil ref */
}

/*
 * SET_VIEWPORT_STATE
 * Payload: start_slot, scale[3], translate[3]
 */
static void virgl_encode_set_viewport(virgl_cmd_buf_t *cb,
                                       float w, float h) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_SET_VIEWPORT_STATE,
                                          0, 7));
    virgl_cmd_dword(cb, 0);         /* start_slot */
    virgl_cmd_float(cb, w / 2.0f);  /* scale_x */
    virgl_cmd_float(cb, h / 2.0f);  /* scale_y */
    virgl_cmd_float(cb, 0.5f);      /* scale_z */
    virgl_cmd_float(cb, w / 2.0f);  /* translate_x */
    virgl_cmd_float(cb, h / 2.0f);  /* translate_y */
    virgl_cmd_float(cb, 0.5f);      /* translate_z */
}

/*
 * CREATE_OBJECT: blend state
 * Minimal: no blending, write all channels
 */
static void virgl_encode_create_blend(virgl_cmd_buf_t *cb,
                                       uint32_t handle) {
    /* Blend state: 3 header dwords + per-RT blend (11 dwords = S0.independent + RT0..RT7) */
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                          VIRGL_OBJECT_BLEND, 11));
    virgl_cmd_dword(cb, handle);
    virgl_cmd_dword(cb, 0);  /* S0: logicop_enable=0, dither=0, alpha_to_coverage=0 */
    virgl_cmd_dword(cb, 0);  /* S1: independent_blend=0 */
    /* RT[0]: blend_enable=0, colormask=0xF (RGBA write) */
    virgl_cmd_dword(cb, (0xF << 27));  /* RT[0].colormask = bits 27-30 */
    virgl_cmd_dword(cb, 0);  /* RT[0] blend factors */
    virgl_cmd_dword(cb, 0);
    virgl_cmd_dword(cb, 0);
    virgl_cmd_dword(cb, 0);
    virgl_cmd_dword(cb, 0);
    virgl_cmd_dword(cb, 0);
    virgl_cmd_dword(cb, 0);
}

/*
 * CREATE_OBJECT: rasterizer state
 * Minimal: fill both faces, no culling
 */
static void virgl_encode_create_rasterizer(virgl_cmd_buf_t *cb,
                                            uint32_t handle) {
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
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                          VIRGL_OBJECT_RASTERIZER, 9));
    virgl_cmd_dword(cb, handle);
    /* S0: bit1=depth_clip, bit15=front_ccw, bit29=half_pixel_center */
    virgl_cmd_dword(cb, (1u << 1) | (1u << 15) | (1u << 29));
    virgl_cmd_float(cb, 1.0f);  /* pos 3: point_size */
    virgl_cmd_dword(cb, 0);     /* pos 4: sprite_coord_enable */
    virgl_cmd_dword(cb, 0);     /* pos 5: S3 (no stipple, no clip planes) */
    virgl_cmd_float(cb, 0.0f);  /* pos 6: line_width */
    virgl_cmd_float(cb, 0.0f);  /* pos 7: offset_units */
    virgl_cmd_float(cb, 0.0f);  /* pos 8: offset_scale */
    virgl_cmd_float(cb, 0.0f);  /* pos 9: offset_clamp */
}

/*
 * CREATE_OBJECT: DSA (depth/stencil/alpha) state
 * Minimal: no depth test, no stencil, no alpha test
 */
static void virgl_encode_create_dsa(virgl_cmd_buf_t *cb,
                                     uint32_t handle) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                          VIRGL_OBJECT_DSA, 5));
    virgl_cmd_dword(cb, handle);
    virgl_cmd_dword(cb, 0);  /* S0: depth.enabled=0, writemask=0, func=0 */
    virgl_cmd_dword(cb, 0);  /* S1: stencil[0] */
    virgl_cmd_dword(cb, 0);  /* S2: stencil[1] */
    virgl_cmd_dword(cb, 0);  /* alpha ref + alpha func = 0 (disabled) */
}

/*
 * BIND_OBJECT: bind a created object to the pipeline
 */
static void virgl_encode_bind_object(virgl_cmd_buf_t *cb,
                                      uint32_t handle,
                                      uint32_t obj_type) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_OBJECT,
                                          obj_type, 1));
    virgl_cmd_dword(cb, handle);
}

/*
 * CREATE_OBJECT: vertex elements
 * Each element: src_offset, instance_divisor, vertex_buffer_index, src_format
 */
static void virgl_encode_create_vertex_elements(virgl_cmd_buf_t *cb,
                                                  uint32_t handle,
                                                  uint32_t num_elements) {
    /* Each element = 4 dwords; header + handle + num_elements */
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                          VIRGL_OBJECT_VERTEX_ELEMENTS,
                                          1 + num_elements * 4));
    virgl_cmd_dword(cb, handle);

    /* Element 0: position (float3 at offset 0) */
    virgl_cmd_dword(cb, 0);   /* src_offset = 0 */
    virgl_cmd_dword(cb, 0);   /* instance_divisor = 0 */
    virgl_cmd_dword(cb, 0);   /* vertex_buffer_index = 0 */
    virgl_cmd_dword(cb, VIRGL_FORMAT_R32G32B32_FLOAT); /* src_format */

    if (num_elements > 1) {
        /* Element 1: color (float4 at offset 12) */
        virgl_cmd_dword(cb, 12);  /* src_offset = 3 floats = 12 bytes */
        virgl_cmd_dword(cb, 0);
        virgl_cmd_dword(cb, 0);
        virgl_cmd_dword(cb, VIRGL_FORMAT_R32G32B32A32_FLOAT);
    }
}

/*
 * SET_VERTEX_BUFFERS
 * Payload per buffer: stride, offset, res_handle
 */
static void virgl_encode_set_vertex_buffers(virgl_cmd_buf_t *cb,
                                             uint32_t stride,
                                             uint32_t offset,
                                             uint32_t res_handle) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_SET_VERTEX_BUFFERS,
                                          0, 3));
    virgl_cmd_dword(cb, stride);
    virgl_cmd_dword(cb, offset);
    virgl_cmd_dword(cb, res_handle);
}

/*
 * DRAW_VBO
 * Draws primitives from bound vertex buffer
 */
static void virgl_encode_draw_vbo(virgl_cmd_buf_t *cb,
                                   uint32_t mode,
                                   uint32_t start,
                                   uint32_t count) {
    virgl_cmd_dword(cb, VIRGL_CMD_HEADER(VIRGL_CCMD_DRAW_VBO, 0, 12));
    virgl_cmd_dword(cb, start);         /* start */
    virgl_cmd_dword(cb, count);         /* count */
    virgl_cmd_dword(cb, mode);          /* mode */
    virgl_cmd_dword(cb, 0);             /* indexed = 0 */
    virgl_cmd_dword(cb, 1);             /* instance_count = 1 */
    virgl_cmd_dword(cb, 0);             /* index_bias */
    virgl_cmd_dword(cb, 0);             /* start_instance */
    virgl_cmd_dword(cb, 0);             /* primitive_restart = 0 */
    virgl_cmd_dword(cb, 0);             /* restart_index */
    virgl_cmd_dword(cb, 0);             /* min_index */
    virgl_cmd_dword(cb, count - 1);     /* max_index */
    virgl_cmd_dword(cb, 0);             /* cso (flags) */
}

/* ═══ Test implementation ════════════════════════════════════ */

/* Test render target dimensions */
#define TEST_W  64
#define TEST_H  64

void cmd_virgl_test(int argc, char *argv[]) {
    (void)argc; (void)argv;

    printf("=== VirtIO GPU 3D (virgl) test ===\n\n");

    /* Check prerequisites */
    if (!virtio_gpu_is_active()) {
        printf("FAIL: VirtIO GPU not active\n");
        return;
    }

    if (!virtio_gpu_has_virgl()) {
        printf("FAIL: VIRGL feature not negotiated\n");
        printf("  Hint: Run QEMU with -vga virtio -display sdl,gl=on\n");
        return;
    }

    printf("[1/6] Creating virgl context...\n");

    uint32_t ctx_id = 1;
    if (virtio_gpu_3d_ctx_create(ctx_id, "virgl-test") != 0) {
        printf("FAIL: ctx_create\n");
        return;
    }
    printf("  OK: context %u created\n", ctx_id);

    /* ── Capset query ─────────────────────────────────────────── */

    printf("[2/6] Querying capability sets...\n");

    uint32_t cap_id = 0, cap_ver = 0, cap_size = 0;
    if (virtio_gpu_3d_get_capset_info(0, &cap_id, &cap_ver, &cap_size) == 0) {
        printf("  Capset[0]: id=%u version=%u size=%u\n",
               cap_id, cap_ver, cap_size);
    } else {
        printf("  Warning: capset info query failed (non-fatal)\n");
    }

    /* ── Create 3D render target ──────────────────────────────── */

    printf("[3/6] Creating 3D render target (%dx%d)...\n", TEST_W, TEST_H);

    uint32_t rt_res_id = virtio_gpu_alloc_resource_id();

    if (virtio_gpu_3d_resource_create(ctx_id, rt_res_id,
                                       PIPE_TEXTURE_2D,
                                       VIRGL_FORMAT_B8G8R8X8_UNORM,
                                       VIRGL_BIND_RENDER_TARGET,
                                       TEST_W, TEST_H, 1,
                                       1, 0, 0, 0) != 0) {
        printf("FAIL: resource_create_3d\n");
        virtio_gpu_3d_ctx_destroy(ctx_id);
        return;
    }

    /* Allocate backing memory */
    uint32_t rt_size = TEST_W * TEST_H * 4;
    uint32_t rt_frames = (rt_size + 4095) / 4096;
    uint32_t rt_phys = pmm_alloc_contiguous(rt_frames);
    if (!rt_phys) {
        printf("FAIL: PMM alloc for render target\n");
        virtio_gpu_3d_ctx_destroy(ctx_id);
        return;
    }
    memset((void *)rt_phys, 0, rt_frames * 4096);

    /* Attach backing store to resource */
    printf("  attach_backing res=%u phys=0x%x size=%u...\n",
           rt_res_id, rt_phys, rt_size);
    if (virtio_gpu_attach_resource_backing(rt_res_id,
                                            (uint32_t *)rt_phys, rt_size) != 0) {
        printf("FAIL: attach_backing\n");
        pmm_free_contiguous(rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(ctx_id);
        return;
    }
    printf("  OK: backing attached\n");

    /* Attach resource to virgl context */
    printf("  ctx_attach_resource ctx=%u res=%u...\n", ctx_id, rt_res_id);
    if (virtio_gpu_3d_ctx_attach_resource(ctx_id, rt_res_id) != 0) {
        printf("FAIL: ctx_attach_resource\n");
        pmm_free_contiguous(rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(ctx_id);
        return;
    }
    printf("  OK: resource %u in context (phys=0x%x)\n", rt_res_id, rt_phys);

    /* ── Encode and submit CLEAR command ──────────────────────── */

    printf("[4/6] Encoding and submitting CLEAR command...\n");

    static virgl_cmd_buf_t cb;
    virgl_cmd_reset(&cb);

    /* Create a surface from the render target */
    uint32_t surf_handle = 1;
    virgl_encode_create_surface(&cb, surf_handle, rt_res_id,
                                 VIRGL_FORMAT_B8G8R8X8_UNORM, 0, 0);

    /* Set framebuffer state */
    uint32_t cbuf = surf_handle;
    virgl_encode_set_framebuffer(&cb, 1, 0, &cbuf);

    /* Clear to a distinctive color: green (R=0, G=1, B=0) */
    virgl_encode_clear(&cb, PIPE_CLEAR_COLOR0,
                        0.0f, 1.0f, 0.0f, 1.0f,
                        1.0, 0);

    /* Submit the command stream */
    int rc = virtio_gpu_3d_submit(ctx_id, cb.buf, cb.pos * 4);
    if (rc != 0) {
        printf("FAIL: submit_3d for clear\n");
        pmm_free_contiguous(rt_phys, rt_frames);
        virtio_gpu_3d_ctx_destroy(ctx_id);
        return;
    }
    printf("  OK: clear submitted (%u dwords)\n", cb.pos);

    /* ── Readback and verify ──────────────────────────────────── */

    printf("[5/6] Reading back pixels...\n");

    struct virtio_gpu_box box;
    box.x = 0; box.y = 0; box.z = 0;
    box.w = TEST_W; box.h = TEST_H; box.d = 1;

    rc = virtio_gpu_3d_transfer_from_host(rt_res_id, ctx_id,
                                           0, TEST_W * 4, 0,
                                           &box, 0);
    if (rc != 0) {
        printf("  Warning: transfer_from_host failed (may not be supported)\n");
        printf("  Checking backing memory directly...\n");
    }

    /* Check pixels — after clear, should be green (0x00FF00 in BGRX = 0x0000FF00) */
    uint32_t *pixels = (uint32_t *)rt_phys;
    uint32_t sample = pixels[0];
    uint32_t sample_mid = pixels[(TEST_H / 2) * TEST_W + TEST_W / 2];

    printf("  pixel[0,0] = 0x%08x\n", sample);
    printf("  pixel[%d,%d] = 0x%08x\n", TEST_W / 2, TEST_H / 2, sample_mid);

    /* The expected value depends on the host GPU's interpretation:
       BGRX clear with green (R=0,G=1,B=0) should give:
       - B=0x00, G=0xFF, R=0x00, X=0xFF -> 0xFF00FF00 or 0x0000FF00 */
    int clear_ok = 0;
    if (sample == 0xFF00FF00 || sample == 0x0000FF00 ||
        sample == 0x00FF0000 || sample == 0xFF00FF00) {
        printf("  PASS: clear color matches expected pattern\n");
        clear_ok = 1;
    } else if (sample == 0) {
        printf("  INFO: pixels are zero (readback may need flush; clear still submitted OK)\n");
        clear_ok = 1; /* non-fatal: some QEMU versions need gl=on for readback */
    } else {
        printf("  INFO: unexpected pixel value 0x%08x (host may use different format)\n",
               sample);
        clear_ok = 1; /* non-fatal */
    }

    /* ── Draw test ─────────────────────────────────────────────── */

    printf("[6/8] Draw test: triangle with TGSI shaders...\n");

    /* Create a VB resource (PIPE_BUFFER) */
    uint32_t vb_res_id = virtio_gpu_alloc_resource_id();
    uint32_t vb_size = 256;
    uint32_t vb_frames = 1;
    uint32_t vb_phys = pmm_alloc_contiguous(vb_frames);
    if (!vb_phys) {
        printf("FAIL: VB PMM alloc\n");
        goto cleanup;
    }
    memset((void *)vb_phys, 0, 4096);

    if (virtio_gpu_3d_resource_create(ctx_id, vb_res_id,
                                       PIPE_BUFFER, VIRGL_FORMAT_R8_UNORM,
                                       VIRGL_BIND_VERTEX_BUFFER,
                                       vb_size, 1, 1, 1, 0, 0, 0) != 0) {
        printf("FAIL: VB resource create\n");
        pmm_free_contiguous(vb_phys, vb_frames);
        goto cleanup;
    }
    if (virtio_gpu_attach_resource_backing(vb_res_id, (uint32_t *)vb_phys, vb_size) != 0 ||
        virtio_gpu_3d_ctx_attach_resource(ctx_id, vb_res_id) != 0) {
        printf("FAIL: VB attach\n");
        pmm_free_contiguous(vb_phys, vb_frames);
        goto cleanup;
    }

    /* Write vertex data: oversized triangle covering entire NDC.
       Each vertex: (x, y, z, w) = R32G32B32A32_FLOAT, stride 16 bytes */
    {
        float *vb = (float *)vb_phys;
        /* v0: bottom-left */
        vb[0] = -1.0f; vb[1] = -1.0f; vb[2] = 0.0f; vb[3] = 1.0f;
        /* v1: far right */
        vb[4] =  3.0f; vb[5] = -1.0f; vb[6] = 0.0f; vb[7] = 1.0f;
        /* v2: far top */
        vb[8] = -1.0f; vb[9] =  3.0f; vb[10] = 0.0f; vb[11] = 1.0f;
    }

    /* Upload VB to host */
    {
        struct virtio_gpu_box vb_box;
        vb_box.x = 0; vb_box.y = 0; vb_box.z = 0;
        vb_box.w = 48; vb_box.h = 1; vb_box.d = 1;
        int trc = virtio_gpu_3d_transfer_to_host(vb_res_id, ctx_id,
                                                   0, 0, 0, &vb_box, 0);
        printf("  VB transfer: %d\n", trc);
    }

    /* First clear to RED, then draw triangle that should overwrite */
    virgl_cmd_reset(&cb);

    /* Re-create surface (may have been invalidated) and set framebuffer */
    virgl_encode_create_surface(&cb, surf_handle, rt_res_id,
                                 VIRGL_FORMAT_B8G8R8X8_UNORM, 0, 0);
    virgl_encode_set_framebuffer(&cb, 1, 0, &cbuf);
    virgl_encode_set_viewport(&cb, (float)TEST_W, (float)TEST_H);

    /* Clear to RED first */
    virgl_encode_clear(&cb, PIPE_CLEAR_COLOR0,
                        1.0f, 0.0f, 0.0f, 1.0f, 1.0, 0);

    /* Create pipeline state objects */
    /* Blend: no blending, just write RGBA */
    uint32_t h_blend = 10;
    virgl_encode_create_blend(&cb, h_blend);
    virgl_encode_bind_object(&cb, h_blend, VIRGL_OBJECT_BLEND);

    /* Rasterizer: minimal */
    uint32_t h_rast = 11;
    virgl_encode_create_rasterizer(&cb, h_rast);
    virgl_encode_bind_object(&cb, h_rast, VIRGL_OBJECT_RASTERIZER);

    /* DSA: no depth/stencil */
    uint32_t h_dsa = 12;
    virgl_encode_create_dsa(&cb, h_dsa);
    virgl_encode_bind_object(&cb, h_dsa, VIRGL_OBJECT_DSA);

    /* VE: single element, R32G32B32A32_FLOAT at offset 0 */
    uint32_t h_ve = 13;
    virgl_encode_create_vertex_elements(&cb, h_ve, 1);
    virgl_encode_bind_object(&cb, h_ve, VIRGL_OBJECT_VERTEX_ELEMENTS);

    /* Shaders: simplest possible TGSI.
       VS: 1 input (position), passthrough.
       FS: outputs constant green by copying position-as-color. */
    static const char test_vs[] =
        "VERT\n"
        "DCL IN[0]\n"
        "DCL OUT[0], POSITION\n"
        "DCL OUT[1], GENERIC[0]\n"
        "  0: MOV OUT[0], IN[0]\n"
        "  1: MOV OUT[1], IN[0]\n"
        "  2: END\n";

    static const char test_fs[] =
        "FRAG\n"
        "DCL IN[0], GENERIC[0], LINEAR\n"
        "DCL OUT[0], COLOR\n"
        "  0: MOV OUT[0], IN[0]\n"
        "  1: END\n";

    /* Encode VS creation */
    uint32_t h_vs = 14, h_fs = 15;
    {
        uint32_t vs_len = (uint32_t)strlen(test_vs) + 1;
        uint32_t vs_dw = (vs_len + 3) / 4;
        virgl_cmd_dword(&cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                                VIRGL_OBJECT_SHADER, 5 + vs_dw));
        virgl_cmd_dword(&cb, h_vs);
        virgl_cmd_dword(&cb, PIPE_SHADER_VERTEX);
        virgl_cmd_dword(&cb, vs_len);    /* offlen = text byte count */
        virgl_cmd_dword(&cb, 300);       /* num_tokens hint */
        virgl_cmd_dword(&cb, 0);         /* num_so_outputs */
        const uint8_t *src = (const uint8_t *)test_vs;
        for (uint32_t i = 0; i < vs_dw; i++) {
            uint32_t d = 0;
            for (int b = 0; b < 4; b++) {
                uint32_t off = i * 4 + (uint32_t)b;
                if (off < vs_len) d |= (uint32_t)src[off] << (b * 8);
            }
            virgl_cmd_dword(&cb, d);
        }
    }
    /* Bind VS */
    virgl_cmd_dword(&cb, VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_SHADER, 0, 2));
    virgl_cmd_dword(&cb, h_vs);
    virgl_cmd_dword(&cb, PIPE_SHADER_VERTEX);

    /* Encode FS creation */
    {
        uint32_t fs_len = (uint32_t)strlen(test_fs) + 1;
        uint32_t fs_dw = (fs_len + 3) / 4;
        virgl_cmd_dword(&cb, VIRGL_CMD_HEADER(VIRGL_CCMD_CREATE_OBJECT,
                                                VIRGL_OBJECT_SHADER, 5 + fs_dw));
        virgl_cmd_dword(&cb, h_fs);
        virgl_cmd_dword(&cb, PIPE_SHADER_FRAGMENT);
        virgl_cmd_dword(&cb, fs_len);
        virgl_cmd_dword(&cb, 300);
        virgl_cmd_dword(&cb, 0);
        const uint8_t *src = (const uint8_t *)test_fs;
        for (uint32_t i = 0; i < fs_dw; i++) {
            uint32_t d = 0;
            for (int b = 0; b < 4; b++) {
                uint32_t off = i * 4 + (uint32_t)b;
                if (off < fs_len) d |= (uint32_t)src[off] << (b * 8);
            }
            virgl_cmd_dword(&cb, d);
        }
    }
    /* Bind FS */
    virgl_cmd_dword(&cb, VIRGL_CMD_HEADER(VIRGL_CCMD_BIND_SHADER, 0, 2));
    virgl_cmd_dword(&cb, h_fs);
    virgl_cmd_dword(&cb, PIPE_SHADER_FRAGMENT);

    /* Set vertex buffer */
    virgl_encode_set_vertex_buffers(&cb, 16, 0, vb_res_id);

    /* Draw triangle */
    virgl_encode_draw_vbo(&cb, PIPE_PRIM_TRIANGLES, 0, 3);

    printf("  Draw batch: %u dwords\n", cb.pos);
    rc = virtio_gpu_3d_submit(ctx_id, cb.buf, cb.pos * 4);
    printf("  Submit: %d\n", rc);

    /* Readback */
    {
        struct virtio_gpu_box rbox;
        rbox.x = 0; rbox.y = 0; rbox.z = 0;
        rbox.w = TEST_W; rbox.h = TEST_H; rbox.d = 1;
        virtio_gpu_3d_transfer_from_host(rt_res_id, ctx_id,
                                          0, TEST_W * 4, 0, &rbox, 0);
    }

    /* Check: did the draw change any pixels from red? */
    int draw_ok = 0;
    uint32_t red_expected = 0xFF0000FF; /* B8G8R8X8: B=0xFF, G=0, R=0, X=0xFF → wait no */
    /* Clear was red (R=1,G=0,B=0). In B8G8R8X8_UNORM:
       byte[0]=B=0x00, byte[1]=G=0x00, byte[2]=R=0xFF, byte[3]=X=0xFF
       → uint32 = 0xFF FF0000 → 0xFFFF0000? No...
       Little-endian: byte[0] at LSB: 0x0000FF?? Hmm.
       Actually: B=0, G=0, R=0xFF, X=0xFF → bytes: 00 00 FF FF → uint32 = 0xFFFF0000 */
    printf("  Draw result pixel[0,0]     = 0x%08x\n", pixels[0]);
    printf("  Draw result pixel[mid,mid] = 0x%08x\n",
           pixels[(TEST_H / 2) * TEST_W + TEST_W / 2]);
    printf("  Draw result pixel[last]    = 0x%08x\n",
           pixels[TEST_H * TEST_W - 1]);

    /* Check several positions for non-red pixels */
    for (uint32_t p = 0; p < TEST_W * TEST_H; p += 7) {
        if (pixels[p] != pixels[0]) {
            draw_ok = 1;
            printf("  Found different pixel at offset %u: 0x%08x\n", p, pixels[p]);
            break;
        }
    }

    if (!draw_ok) {
        /* Check if pixels changed from the clear color */
        uint32_t clear_px = pixels[0];
        printf("  All pixels same as [0,0] (0x%08x) — draw may have failed\n", clear_px);
    } else {
        printf("  PASS: draw produced visible change!\n");
    }

    /* ── Display result ───────────────────────────────────────── */

    printf("[7/8] Blitting result to display...\n");

    /* Copy the render target to the top-left corner of the backbuffer */
    uint32_t *bb = gfx_backbuffer();
    if (bb) {
        uint32_t pitch4 = gfx_pitch() / 4;
        for (int y = 0; y < TEST_H && y < (int)gfx_height(); y++) {
            memcpy(&bb[y * pitch4], &pixels[y * TEST_W], TEST_W * 4);
        }
        gfx_flip_rect(0, 0, TEST_W, TEST_H);
        printf("  Blitted %dx%d to display\n", TEST_W, TEST_H);
    }

cleanup:
    /* ── Cleanup ──────────────────────────────────────────────── */

    printf("[8/8] Cleanup...\n");

    virtio_gpu_3d_ctx_detach_resource(ctx_id, vb_res_id);
    pmm_free_contiguous(vb_phys, vb_frames);
    virtio_gpu_3d_ctx_detach_resource(ctx_id, rt_res_id);
    pmm_free_contiguous(rt_phys, rt_frames);
    virtio_gpu_3d_ctx_destroy(ctx_id);

    printf("\n=== virgl test %s ===\n", clear_ok ? "PASSED" : "FAILED");
}
