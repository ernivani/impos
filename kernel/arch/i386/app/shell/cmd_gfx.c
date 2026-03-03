#include <kernel/shell_cmd.h>
#include <kernel/shell.h>
#include <kernel/sh_parse.h>
#include <kernel/gfx.h>
#include <kernel/desktop.h>
#include <kernel/wm.h>
#include <kernel/drm.h>
#include <kernel/libdrm.h>
#include <kernel/mouse.h>
#include <kernel/ui_event.h>
#include <kernel/pmm.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/image.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

/* ═══ Integer sine/cosine table (0..63 → 0..255, quarter-wave) ══ */
static const int16_t sin_tab64[65] = {
      0,  6, 13, 19, 25, 31, 37, 44, 50, 56, 62, 68, 74, 80, 86, 92,
     97,103,109,114,120,125,130,136,141,146,150,155,160,164,169,173,
    177,181,185,189,193,196,200,203,206,209,212,215,218,220,223,225,
    227,229,231,233,234,236,237,238,240,241,241,242,243,243,243,244,244
};

/* Returns sin(angle)*256 where angle is 0..1023 for full circle */
static int isin(int angle) {
    angle = angle & 1023;
    int q = angle >> 8;          /* quadrant 0-3 */
    int idx = angle & 255;       /* position in quadrant (0-255) */
    /* Map 0-255 to 0-64 table index */
    int ti = idx >> 2;           /* 0..63 */
    int val = sin_tab64[ti];
    switch (q) {
        case 0: return  val;
        case 1: return  sin_tab64[64 - ti];
        case 2: return -val;
        case 3: return -sin_tab64[64 - ti];
    }
    return 0;
}
static int icos(int angle) { return isin(angle + 256); }

/* Interpolate between two colors by t (0..255) */
static uint32_t color_lerp(uint32_t a, uint32_t b, int t) {
    int ra = (a >> 16) & 0xFF, ga = (a >> 8) & 0xFF, ba = a & 0xFF;
    int rb = (b >> 16) & 0xFF, gb = (b >> 8) & 0xFF, bb = b & 0xFF;
    int r = ra + (rb - ra) * t / 255;
    int g = ga + (gb - ga) * t / 255;
    int bl = ba + (bb - ba) * t / 255;
    return GFX_RGB(r, g, bl);
}

/* HSV to RGB: h 0..1023, s,v 0..255 */
static uint32_t hsv_to_rgb(int h, int s, int v) {
    h = h & 1023;
    int region = h * 6 / 1024;
    int remainder = (h * 6 - region * 1024) * 255 / 1024;
    int p = v * (255 - s) / 255;
    int q = v * (255 - (s * remainder / 255)) / 255;
    int t2 = v * (255 - (s * (255 - remainder) / 255)) / 255;
    switch (region) {
        case 0:  return GFX_RGB(v, t2, p);
        case 1:  return GFX_RGB(q, v, p);
        case 2:  return GFX_RGB(p, v, t2);
        case 3:  return GFX_RGB(p, q, v);
        case 4:  return GFX_RGB(t2, p, v);
        default: return GFX_RGB(v, p, q);
    }
}

/* Draw animated background gradient */
static void demo_draw_bg(int W, int H, int frame) {
    /* Shifting dark gradient */
    int phase = frame * 2;
    for (int y = 0; y < H; y++) {
        int t = y * 255 / H;
        int r = 8 + (isin(phase + y) * 8 / 256);
        int g = 10 + (isin(phase + y + 200) * 6 / 256);
        int b = 25 + t * 20 / 255 + (isin(phase + y + 400) * 5 / 256);
        if (r < 0) r = 0;
        if (g < 0) g = 0;
        if (b < 0) b = 0;
        if (r > 255) r = 255;
        if (g > 255) g = 255;
        if (b > 255) b = 255;
        gfx_fill_rect(0, y, W, 1, GFX_RGB(r, g, b));
    }
}

/* Scene 1: Orbiting circles with trails */
static void demo_scene_orbits(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    int cx = W / 2, cy = H / 2 - 30;

    /* Title */
    gfx_draw_string_scaled(cx - gfx_string_scaled_w("Orbital Motion", 3) / 2,
                           40, "Orbital Motion", GFX_WHITE, 3);

    /* Central glow */
    for (int r = 50; r > 0; r -= 2) {
        int a = 10 + (50 - r) * 3;
        if (a > 200) a = 200;
        gfx_fill_circle_aa(cx, cy, r, GFX_RGB(a/3, a/4, a));
    }

    /* Orbit rings */
    for (int ring = 0; ring < 4; ring++) {
        int radius = 100 + ring * 70;
        gfx_circle_ring(cx, cy, radius, 1, GFX_RGB(40, 45, 60));
    }

    /* Orbiting bodies with trails */
    for (int i = 0; i < 6; i++) {
        int radius = 100 + (i % 4) * 70;
        int speed = 3 + i * 2;
        int angle = frame * speed + i * 170;
        int hue = (i * 170) & 1023;
        uint32_t col = hsv_to_rgb(hue, 220, 255);

        /* Trail: 8 ghost positions */
        for (int t = 7; t >= 0; t--) {
            int ta = angle - t * speed * 2;
            int tx = cx + icos(ta) * radius / 244;
            int ty = cy + isin(ta) * radius / 244;
            int tr = 8 - t;
            int alpha = (8 - t) * 18;
            gfx_fill_circle_aa(tx, ty, tr, color_lerp(GFX_BLACK, col, alpha));
        }

        /* Main body */
        int bx = cx + icos(angle) * radius / 244;
        int by = cy + isin(angle) * radius / 244;
        gfx_fill_circle_aa(bx, by, 10, col);
        gfx_fill_circle_aa(bx - 2, by - 2, 4, GFX_WHITE);
    }

    /* FPS counter */
    char fps_str[32];
    snprintf(fps_str, sizeof(fps_str), "frame %d", frame);
    gfx_draw_string(10, H - 20, fps_str, GFX_RGB(100, 100, 100), GFX_BLACK);
}

/* Scene 2: Particle fountain */
#define DEMO_MAX_PARTICLES 120
static struct { int x, y, vx, vy; uint32_t col; int life; } particles[DEMO_MAX_PARTICLES];
static int particle_init_done = 0;

static void demo_scene_particles(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Particle System", 3) / 2,
                           40, "Particle System", GFX_WHITE, 3);

    if (!particle_init_done) {
        memset(particles, 0, sizeof(particles));
        particle_init_done = 1;
    }

    /* Spawn new particles from center-bottom */
    for (int i = 0; i < DEMO_MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) {
            particles[i].x = (W / 2) * 256;
            particles[i].y = (H - 120) * 256;
            int spread = (frame * 7 + i * 31) % 512 - 256;
            particles[i].vx = spread;
            particles[i].vy = -600 - ((frame * 13 + i * 17) % 400);
            particles[i].col = hsv_to_rgb((frame * 4 + i * 40) & 1023, 240, 255);
            particles[i].life = 60 + (i * 7) % 40;
            break;
        }
    }

    /* Update and draw */
    for (int i = 0; i < DEMO_MAX_PARTICLES; i++) {
        if (particles[i].life <= 0) continue;
        particles[i].x += particles[i].vx;
        particles[i].y += particles[i].vy;
        particles[i].vy += 10; /* gravity */
        particles[i].life--;

        int px = particles[i].x / 256;
        int py = particles[i].y / 256;
        int sz = 2 + particles[i].life / 20;
        int fade = particles[i].life * 255 / 100;
        if (fade > 255) fade = 255;

        uint32_t c = color_lerp(GFX_BLACK, particles[i].col, fade);
        gfx_fill_circle_aa(px, py, sz, c);
    }

    /* Emitter glow */
    for (int r = 30; r > 0; r -= 3) {
        int a = (30 - r) * 6;
        uint32_t gc = hsv_to_rgb((frame * 6) & 1023, 200, a > 255 ? 255 : a);
        gfx_fill_circle_aa(W / 2, H - 120, r, gc);
    }
}

/* Scene 3: Card showcase (rounded rects, alpha, smooth text) */
static void demo_scene_cards(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Modern UI", 3) / 2,
                           40, "Modern UI", GFX_WHITE, 3);

    /* Floating cards */
    struct { const char *title; const char *sub; uint32_t accent; } cards[] = {
        { "Graphics",  "Shapes & AA", GFX_RGB(88, 166, 255) },
        { "Alpha",     "Transparency", GFX_RGB(255, 120, 88) },
        { "Smooth",    "SDF Fonts",   GFX_RGB(88, 255, 166) },
        { "Animate",   "60+ FPS",     GFX_RGB(200, 130, 255) },
    };

    int card_w = 280, card_h = 320, gap = 40;
    int total_w = 4 * card_w + 3 * gap;
    int start_x = (W - total_w) / 2;
    int base_y = 120;

    for (int i = 0; i < 4; i++) {
        /* Floating animation: each card bobs independently */
        int bob = isin(frame * 4 + i * 256) * 15 / 244;
        int cx = start_x + i * (card_w + gap);
        int cy = base_y + bob;

        /* Card shadow */
        gfx_rounded_rect_alpha(cx + 6, cy + 8, card_w, card_h, 16,
                               GFX_RGB(0, 0, 0), 80);

        /* Card body */
        gfx_rounded_rect(cx, cy, card_w, card_h, 16, GFX_RGB(30, 33, 40));
        gfx_rounded_rect_outline(cx, cy, card_w, card_h, 16, GFX_RGB(55, 60, 75));

        /* Accent bar at top */
        gfx_fill_rect(cx + 20, cy + 16, card_w - 40, 4, cards[i].accent);

        /* Icon area: animated circle */
        int icon_cx = cx + card_w / 2;
        int icon_cy = cy + 90;
        int pulse = 30 + isin(frame * 6 + i * 200) * 8 / 244;
        gfx_fill_circle_aa(icon_cx, icon_cy, pulse, cards[i].accent);
        gfx_fill_circle_aa(icon_cx, icon_cy, pulse - 8, GFX_RGB(30, 33, 40));

        /* Spinning ring */
        int ring_r = pulse + 12;
        gfx_circle_ring(icon_cx, icon_cy, ring_r, 2, cards[i].accent);

        /* Orbiting dot */
        int dot_a = frame * 8 + i * 256;
        int dot_x = icon_cx + icos(dot_a) * ring_r / 244;
        int dot_y = icon_cy + isin(dot_a) * ring_r / 244;
        gfx_fill_circle_aa(dot_x, dot_y, 5, GFX_WHITE);

        /* Title text */
        int tw = gfx_string_scaled_w(cards[i].title, 2);
        gfx_draw_string_scaled(icon_cx - tw / 2, cy + 160,
                               cards[i].title, GFX_WHITE, 2);

        /* Subtitle */
        int sw = gfx_string_scaled_w(cards[i].sub, 1);
        gfx_draw_string(icon_cx - sw / 2, cy + 200,
                         cards[i].sub, GFX_RGB(140, 145, 160), GFX_RGB(30, 33, 40));

        /* Faux progress bar */
        int bar_y = cy + 240;
        int bar_w = card_w - 60;
        int bar_x = cx + 30;
        gfx_rounded_rect(bar_x, bar_y, bar_w, 8, 4, GFX_RGB(45, 48, 58));
        int fill_w = (isin(frame * 3 + i * 300) + 244) * bar_w / 488;
        if (fill_w < 8) fill_w = 8;
        gfx_rounded_rect(bar_x, bar_y, fill_w, 8, 4, cards[i].accent);

        /* Bottom stat numbers */
        char stat[16];
        int pct = (isin(frame * 3 + i * 300) + 244) * 100 / 488;
        snprintf(stat, sizeof(stat), "%d%%", pct);
        gfx_draw_string(bar_x + bar_w + 8, bar_y - 4, stat,
                         cards[i].accent, GFX_RGB(30, 33, 40));
    }
}

/* Scene 4: Wave visualizer */
static void demo_scene_waves(int W, int H, int frame) {
    demo_draw_bg(W, H, frame);

    gfx_draw_string_scaled(W / 2 - gfx_string_scaled_w("Wave Synthesis", 3) / 2,
                           40, "Wave Synthesis", GFX_WHITE, 3);

    int cy = H / 2;

    /* Draw 5 layered waves */
    for (int wave = 0; wave < 5; wave++) {
        int amp = 60 - wave * 8;
        int freq = 3 + wave;
        int speed = 4 + wave * 2;
        uint32_t col = hsv_to_rgb((wave * 200 + frame * 3) & 1023, 200, 220);
        int prev_y = cy;

        for (int x = 0; x < W; x += 2) {
            int angle = x * freq + frame * speed;
            int y = cy + isin(angle) * amp / 244 +
                    isin(angle * 2 + frame * 3) * (amp / 3) / 244;

            /* Fill from wave to bottom with alpha */
            int fill_h = H - y;
            if (fill_h > 0) {
                uint32_t fill_col = GFX_RGBA(
                    (col >> 16) & 0xFF,
                    (col >> 8) & 0xFF,
                    col & 0xFF,
                    20 + wave * 10);
                gfx_fill_rect_alpha(x, y, 2, fill_h > 200 ? 200 : fill_h, fill_col);
            }

            /* Wave line */
            if (x > 0)
                gfx_draw_line(x - 2, prev_y, x, y, col);
            prev_y = y;
        }
    }

    /* Pulsing center orb */
    int orb_r = 40 + isin(frame * 8) * 15 / 244;
    for (int r = orb_r; r > 0; r -= 2) {
        int brightness = (orb_r - r) * 200 / orb_r;
        gfx_fill_circle_aa(W / 2, cy, r,
                           GFX_RGB(brightness / 2, brightness, brightness));
    }

    /* Frequency bars at bottom */
    int bar_count = 32;
    int bar_w = (W - 100) / bar_count;
    int bar_base = H - 80;
    for (int i = 0; i < bar_count; i++) {
        int bh = 20 + (isin(frame * 6 + i * 32) + 244) * 40 / 488;
        uint32_t bc = hsv_to_rgb((i * 32 + frame * 4) & 1023, 240, 230);
        int bx = 50 + i * bar_w;
        gfx_rounded_rect(bx, bar_base - bh, bar_w - 2, bh, 3, bc);
    }
}

static void cmd_drmtest(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    printf("=== DRM Test via libdrm API ===\n\n");

    /* Step 1: drmOpen */
    printf("[1] drmOpen... ");
    int fd = drmOpen("impos-drm", NULL);
    if (fd < 0) {
        printf("FAIL (DRM not available)\n");
        return;
    }
    printf("OK (fd=%d)\n", fd);

    /* Step 2: drmGetVersion */
    printf("[2] drmGetVersion... ");
    drmVersionPtr ver = drmGetVersion(fd);
    if (ver) {
        printf("OK - %s v%d.%d.%d\n",
               ver->name, ver->version_major,
               ver->version_minor, ver->version_patchlevel);
        drmFreeVersion(ver);
    } else {
        printf("FAIL\n");
    }

    /* Step 3: drmGetCap */
    printf("[3] drmGetCap(DUMB_BUFFER)... ");
    uint64_t cap_val = 0;
    int rc = drmGetCap(fd, DRM_CAP_DUMB_BUFFER, &cap_val);
    printf("%s (val=%llu)\n", rc == 0 ? "OK" : "FAIL",
           (unsigned long long)cap_val);

    /* Step 4: drmModeGetResources */
    printf("[4] drmModeGetResources... ");
    drmModeResPtr res = drmModeGetResources(fd);
    if (!res) {
        printf("FAIL\n");
        goto done;
    }
    printf("OK (crtc=%d conn=%d enc=%d)\n",
           res->count_crtcs, res->count_connectors, res->count_encoders);

    if (res->count_connectors == 0 || res->count_crtcs == 0) {
        printf("    No connectors/CRTCs — cannot continue.\n");
        drmModeFreeResources(res);
        goto done;
    }

    uint32_t conn_id = res->connectors[0];
    uint32_t crtc_id = res->crtcs[0];

    /* Step 5: drmModeGetConnector */
    printf("[5] drmModeGetConnector(%u)... ", conn_id);
    drmModeConnectorPtr conn = drmModeGetConnector(fd, conn_id);
    if (!conn) {
        printf("FAIL\n");
        drmModeFreeResources(res);
        goto done;
    }
    printf("OK (%d modes, %s)\n", conn->count_modes,
           conn->connection == DRM_MODE_CONNECTED ? "connected" : "disconnected");
    for (int i = 0; i < conn->count_modes && i < 4; i++) {
        const char *pref = (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) ? "*" : " ";
        printf("    %s %s %ux%u@%uHz\n", pref,
               conn->modes[i].name, conn->modes[i].hdisplay,
               conn->modes[i].vdisplay, conn->modes[i].vrefresh);
    }

    /* Step 6: drmModeGetEncoder */
    printf("[6] drmModeGetEncoder(%u)... ", conn->encoder_id);
    drmModeEncoderPtr enc = drmModeGetEncoder(fd, conn->encoder_id);
    if (enc) {
        printf("OK (crtc=%u)\n", enc->crtc_id);
        drmModeFreeEncoder(enc);
    } else {
        printf("FAIL\n");
    }

    /* Step 7: drmModeGetCrtc */
    printf("[7] drmModeGetCrtc(%u)... ", crtc_id);
    drmModeCrtcPtr crtc = drmModeGetCrtc(fd, crtc_id);
    if (crtc) {
        printf("OK (fb=%u, mode=%s)\n", crtc->buffer_id,
               crtc->mode_valid ? crtc->mode.name : "none");
        drmModeFreeCrtc(crtc);
    } else {
        printf("FAIL\n");
    }

    /* ── Stage 2: GEM pipeline via libdrm ────────────────────── */
    printf("\n--- GEM Dumb Buffer Pipeline ---\n");

    /* Step 8: drmModeCreateDumbBuffer */
    printf("[8] drmModeCreateDumbBuffer(64x64)... ");
    uint32_t handle = 0, pitch = 0;
    uint64_t size = 0;
    rc = drmModeCreateDumbBuffer(fd, 64, 64, 32, 0, &handle, &pitch, &size);
    if (rc != 0) {
        printf("FAIL\n");
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        goto done;
    }
    printf("OK (handle=%u, pitch=%u, size=%llu)\n",
           handle, pitch, (unsigned long long)size);

    /* Step 9: drmModeMapDumbBuffer */
    printf("[9] drmModeMapDumbBuffer(%u)... ", handle);
    uint64_t offset = 0;
    rc = drmModeMapDumbBuffer(fd, handle, &offset);
    if (rc != 0) {
        printf("FAIL\n");
        drmModeDestroyDumbBuffer(fd, handle);
        drmModeFreeConnector(conn);
        drmModeFreeResources(res);
        goto done;
    }
    printf("OK (offset=0x%llx)\n", (unsigned long long)offset);

    /* Step 10: Write pixels */
    printf("[10] Writing red pixels... ");
    uint32_t *pixels = (uint32_t *)(uint32_t)offset;
    uint32_t npixels = pitch / 4 * 64;
    for (uint32_t i = 0; i < npixels; i++)
        pixels[i] = 0xFFFF0000;
    int ok = (pixels[0] == 0xFFFF0000 && pixels[npixels - 1] == 0xFFFF0000);
    printf("%s\n", ok ? "OK (verified)" : "FAIL");

    /* Step 11: drmModeAddFB */
    printf("[11] drmModeAddFB... ");
    uint32_t fb_id = 0;
    rc = drmModeAddFB(fd, 64, 64, 24, 32, pitch, handle, &fb_id);
    if (rc != 0) { printf("FAIL\n"); goto gem_cleanup; }
    printf("OK (fb_id=%u)\n", fb_id);

    /* Step 12: drmModePageFlip */
    printf("[12] drmModePageFlip(fb=%u)... ", fb_id);
    rc = drmModePageFlip(fd, crtc_id, fb_id, 0, NULL);
    printf("%s\n", rc == 0 ? "OK (flipped!)" : "FAIL");

    /* Step 13: drmModeRmFB */
    printf("[13] drmModeRmFB(%u)... ", fb_id);
    rc = drmModeRmFB(fd, fb_id);
    printf("%s\n", rc == 0 ? "OK" : "FAIL");

gem_cleanup:
    /* Step 14: drmModeDestroyDumbBuffer */
    printf("[14] drmModeDestroyDumbBuffer(%u)... ", handle);
    rc = drmModeDestroyDumbBuffer(fd, handle);
    printf("%s\n", rc == 0 ? "OK" : "FAIL");

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

done:
    /* Step 15: drmClose */
    drmClose(fd);
    printf("[15] drmClose... OK\n");

    printf("\n=== All libdrm tests passed ===\n");
}

static void cmd_gfxdemo(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available (text mode fallback)\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    /* Suspend WM compositing so the demo owns the framebuffer */
    keyboard_set_idle_callback(0);

    particle_init_done = 0;
    int scene = 0;
    int frame = 0;
    uint32_t start_tick = pit_get_ticks();
    int total_scenes = 4;
    int demo_tid = task_register("gfxdemo", 1, -1);

    while (1) {
        uint32_t frame_start = pit_get_ticks();

        /* Draw current scene */
        switch (scene) {
            case 0: demo_scene_orbits(W, H, frame); break;
            case 1: demo_scene_particles(W, H, frame); break;
            case 2: demo_scene_cards(W, H, frame); break;
            case 3: demo_scene_waves(W, H, frame); break;
        }

        /* Scene indicator dots */
        int dot_y = H - 40;
        int dot_cx = W / 2;
        for (int i = 0; i < total_scenes; i++) {
            int dx = dot_cx + (i - total_scenes / 2) * 24 + 12;
            if (i == scene) {
                gfx_fill_circle_aa(dx, dot_y, 6, GFX_WHITE);
            } else {
                gfx_circle_ring(dx, dot_y, 6, 2, GFX_RGB(100, 100, 110));
            }
        }

        /* Help text */
        gfx_draw_string(W / 2 - 140, H - 20,
                         "SPACE: next scene  Q: quit",
                         GFX_RGB(120, 125, 140), GFX_BLACK);

        gfx_flip();
        frame++;

        /* Auto-advance scene every ~8 seconds (960 ticks at 120Hz) */
        if ((pit_get_ticks() - start_tick) > 960) {
            scene = (scene + 1) % total_scenes;
            start_tick = pit_get_ticks();
            particle_init_done = 0;
        }

        /* Check for killed flag */
        if (demo_tid >= 0 && task_check_killed(demo_tid)) break;

        /* Check for input (non-blocking) */
        if (keyboard_data_available()) {
            char c = getchar();
            if (c == 'q' || c == 'Q' || c == 27) break;
            if (c == ' ' || c == '\n') {
                scene = (scene + 1) % total_scenes;
                start_tick = pit_get_ticks();
                particle_init_done = 0;
            }
        }

        /* Cap at ~30fps: wait for at least 4 ticks */
        while (pit_get_ticks() - frame_start < 4) {
            task_set_current(TASK_IDLE);
            cpu_halting = 1;
            __asm__ volatile ("hlt");
            cpu_halting = 0;
        }
        task_set_current(TASK_SHELL);
    }

    /* Unregister process */
    if (demo_tid >= 0) task_unregister(demo_tid);

    /* Restore idle callback, then full WM composite to redraw desktop */
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active())
        wm_composite();
}

/* ═══ gfxbench — max-throughput rendering stress test ═════════ */

static uint32_t bench_seed;
static uint32_t bench_brand(void) {
    bench_seed = bench_seed * 1103515245 + 12345;
    return (bench_seed >> 16) & 0x7FFF;
}

static void cmd_gfxbench(int argc, char* argv[]) {
    (void)argc; (void)argv;

    if (!gfx_is_active()) {
        printf("Graphics mode not available\n");
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    keyboard_set_idle_callback(0);
    int tid = task_register("gfxbench", 1, -1);

    bench_seed = pit_get_ticks() ^ 0xDEADBEEF;
    uint32_t frame = 0;
    uint32_t fps = 0;
    uint32_t sec_start = pit_get_ticks();
    uint32_t frames_this_sec = 0;
    int phase = 0;
    uint32_t phase_start = pit_get_ticks();
    int total_phases = 5;
    uint32_t phase_fps[5] = {0};
    uint32_t phase_pixels_ok[5] = {0};
    uint32_t phase_frames[5] = {0};
    int quit = 0;

    static const char *phase_names[] = {
        "Rect Flood", "Line Storm", "Circle Cascade",
        "Alpha Blend", "Combined Chaos"
    };

    while (!quit) {
        uint32_t now = pit_get_ticks();

        /* FPS every second */
        if (now - sec_start >= 120) {
            fps = frames_this_sec * 120 / (now - sec_start);
            phase_fps[phase] = fps;
            frames_this_sec = 0;
            sec_start = now;
        }

        /* Auto-advance phase every 5 seconds (600 ticks at 120Hz) */
        if (now - phase_start >= 600) {
            phase++;
            if (phase >= total_phases) break;
            phase_start = now;
        }

        gfx_clear(GFX_BLACK);

        /* Inline PRNG */
        #define BRAND() bench_brand()

        switch (phase) {
        case 0: /* Rect flood — 200 random filled rects */
            for (int i = 0; i < 200; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 10 + (int)(BRAND() % 200);
                int rh = 10 + (int)(BRAND() % 200);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 7) & 1023, 200, 220);
                gfx_fill_rect(rx, ry, rw, rh, c);
            }
            break;

        case 1: /* Line storm — 500 random lines */
            for (int i = 0; i < 500; i++) {
                int x0 = (int)(BRAND() % (unsigned)W);
                int y0 = (int)(BRAND() % (unsigned)H);
                int x1 = (int)(BRAND() % (unsigned)W);
                int y1 = (int)(BRAND() % (unsigned)H);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 5) & 1023, 240, 255);
                gfx_draw_line(x0, y0, x1, y1, c);
            }
            break;

        case 2: /* Circle cascade — 100 random circles */
            for (int i = 0; i < 100; i++) {
                int ccx = (int)(BRAND() % (unsigned)W);
                int ccy = (int)(BRAND() % (unsigned)H);
                int r = 5 + (int)(BRAND() % 80);
                uint32_t c = hsv_to_rgb((i * 10 + (int)frame * 8) & 1023, 220, 240);
                gfx_fill_circle(ccx, ccy, r, c);
            }
            break;

        case 3: /* Alpha blend stress — 150 overlapping translucent rects */
            for (int i = 0; i < 150; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 20 + (int)(BRAND() % 300);
                int rh = 20 + (int)(BRAND() % 300);
                uint32_t c = hsv_to_rgb(((int)BRAND() + (int)frame * 3) & 1023, 200, 200);
                uint8_t a = (uint8_t)(60 + BRAND() % 140);
                gfx_fill_rect_alpha(rx, ry, rw, rh,
                    GFX_RGBA((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF, a));
            }
            break;

        case 4: /* Combined chaos — rects + lines + circles */
            for (int i = 0; i < 80; i++) {
                int rx = (int)(BRAND() % (unsigned)W);
                int ry = (int)(BRAND() % (unsigned)H);
                int rw = 10 + (int)(BRAND() % 150);
                int rh = 10 + (int)(BRAND() % 150);
                gfx_fill_rect(rx, ry, rw, rh,
                    hsv_to_rgb(((int)BRAND() + (int)frame * 6) & 1023, 200, 200));
            }
            for (int i = 0; i < 200; i++) {
                gfx_draw_line(
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    hsv_to_rgb(((int)BRAND() + (int)frame * 4) & 1023, 240, 255));
            }
            for (int i = 0; i < 30; i++) {
                gfx_fill_circle(
                    (int)(BRAND() % (unsigned)W), (int)(BRAND() % (unsigned)H),
                    5 + (int)(BRAND() % 50),
                    hsv_to_rgb(((int)BRAND() + (int)frame * 9) & 1023, 220, 230));
            }
            break;
        }

        #undef BRAND

        /* HUD bar */
        gfx_fill_rect(0, 0, W, 50, GFX_RGB(0, 0, 0));
        char buf[128];
        snprintf(buf, sizeof(buf), "Phase %d/%d: %s   FPS: %u   Frame: %u",
                 phase + 1, total_phases, phase_names[phase],
                 (unsigned)fps, (unsigned)frame);
        gfx_draw_string(10, 8, buf, GFX_WHITE, GFX_BLACK);

        /* Progress bar */
        int elapsed = (int)(now - phase_start);
        int bar_w = W - 20;
        int fill = elapsed * bar_w / 600;
        if (fill > bar_w) fill = bar_w;
        gfx_fill_rect(10, 34, bar_w, 8, GFX_RGB(40, 40, 50));
        gfx_fill_rect(10, 34, fill, 8, GFX_RGB(80, 160, 255));

        gfx_draw_string(W - 18 * FONT_W, 8,
                         "Q: quit early", GFX_RGB(120, 125, 140), GFX_BLACK);

        gfx_flip();
        frame++;
        frames_this_sec++;

        if (tid >= 0 && task_check_killed(tid)) break;

        int key = keyboard_getchar_nb();
        if (key == 'q' || key == 'Q' || key == 27) { quit = 1; break; }

        /* Validation: sample a pixel from backbuffer after drawing */
        {
            uint32_t *bb = gfx_backbuffer();
            int sx = W / 2, sy = H / 2;
            uint32_t px = bb[sy * (int)(gfx_pitch() / 4) + sx];
            if (px != 0) phase_pixels_ok[phase]++;
            phase_frames[phase]++;
        }

        /* No frame cap — maximum throughput */
    }

    if (tid >= 0) task_unregister(tid);
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();

    /* Print results summary with validation */
    printf("=== Graphics Benchmark Results ===\n");
    int all_pass = 1;
    for (int i = 0; i < total_phases; i++) {
        if (i > phase) break;
        int drawn = (phase_frames[i] > 0 && phase_pixels_ok[i] > 0);
        int rate_ok = (phase_fps[i] > 0);
        int pass = drawn && rate_ok;
        if (!pass) all_pass = 0;
        printf("  %-18s %4u fps  %5u frames  %s\n",
               phase_names[i], (unsigned)phase_fps[i],
               (unsigned)phase_frames[i],
               pass ? "PASS" : "FAIL");
    }
    printf("  Total frames: %u\n", (unsigned)frame);
    printf("  Result: %s\n", all_pass ? "ALL PASS" : "SOME FAILED");
    printf("==================================\n");

    if (gfx_is_active()) wm_composite();
}

/* ═══ fps — toggle FPS overlay on desktop ═════════════════════ */

static void cmd_fps(int argc, char* argv[]) {
    (void)argc; (void)argv;
    wm_toggle_fps();
    printf("FPS overlay: %s\n", wm_fps_enabled() ? "ON" : "OFF");
    wm_composite();
}

/* ═══ imgview — display an image file fullscreen ═════════════════ */

static void cmd_imgview(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: imgview <path>\n");
        return;
    }

    if (!gfx_is_active()) {
        printf("Graphics mode not available\n");
        return;
    }

    image_t *img = image_load_file(argv[1]);
    if (!img) {
        printf("imgview: cannot load image '%s'\n", argv[1]);
        return;
    }

    int W = (int)gfx_width();
    int H = (int)gfx_height();

    /* Scale to fit screen while preserving aspect ratio */
    int scale_w = W * 1024 / img->width;
    int scale_h = H * 1024 / img->height;
    int scale = (scale_w < scale_h) ? scale_w : scale_h;
    int sw = img->width * scale / 1024;
    int sh = img->height * scale / 1024;
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;

    image_t *scaled = image_scale(img, sw, sh);
    image_free(img);
    if (!scaled) {
        printf("imgview: failed to scale image\n");
        return;
    }

    /* Suspend WM and take over framebuffer */
    keyboard_set_idle_callback(0);
    int tid = task_register("imgview", 1, -1);

    gfx_clear(0xFF000000);

    /* Center on screen */
    int ox = (W - sw) / 2;
    int oy = (H - sh) / 2;

    uint32_t *bb = gfx_backbuffer();
    int pitch = (int)(gfx_pitch() / 4);
    for (int y = 0; y < sh && (oy + y) < H; y++) {
        if (oy + y < 0) continue;
        for (int x = 0; x < sw && (ox + x) < W; x++) {
            if (ox + x < 0) continue;
            bb[(oy + y) * pitch + (ox + x)] = scaled->pixels[y * sw + x];
        }
    }

    /* Info bar at bottom */
    char info[128];
    snprintf(info, sizeof(info), "%s  %dx%d  (press any key to exit)",
             argv[1], scaled->width, scaled->height);
    gfx_draw_string(10, H - 20, info, GFX_RGB(180, 180, 180), GFX_BLACK);

    gfx_flip();
    image_free(scaled);

    /* Wait for keypress or kill */
    while (1) {
        if (tid >= 0 && task_check_killed(tid)) break;
        if (keyboard_data_available()) {
            char c = getchar();
            (void)c;
            break;
        }
        task_set_current(TASK_IDLE);
        cpu_halting = 1;
        __asm__ volatile ("hlt");
        cpu_halting = 0;
    }

    if (tid >= 0) task_unregister(tid);
    keyboard_set_idle_callback(desktop_get_idle_terminal_cb());
    terminal_clear();
    if (gfx_is_active()) wm_composite();
}

/* Forward declaration for virgl_test (defined in app/virgl_test.c) */
extern void cmd_virgl_test(int argc, char* argv[]);

static const command_t gfx_commands[] = {
    {
        "gfxdemo", cmd_gfxdemo,
        "Run a graphics framebuffer demo",
        "gfxdemo: gfxdemo\n"
        "    Draw shapes and colors using the VBE framebuffer.\n",
        "NAME\n"
        "    gfxdemo - graphics demo\n\n"
        "SYNOPSIS\n"
        "    gfxdemo\n\n"
        "DESCRIPTION\n"
        "    Demonstrates the framebuffer graphics API by drawing\n"
        "    rectangles, lines, and text. Press any key to exit.\n",
        CMD_FLAG_ROOT
    },
    {
        "drmtest", cmd_drmtest,
        "Test DRM GPU subsystem",
        "drmtest: drmtest\n"
        "    Open /dev/dri/card0, query DRM version via ioctl, test mmap.\n",
        "NAME\n"
        "    drmtest - test DRM subsystem\n\n"
        "SYNOPSIS\n"
        "    drmtest\n\n"
        "DESCRIPTION\n"
        "    Opens /dev/dri/card0, issues DRM_IOCTL_VERSION to query the\n"
        "    GPU driver, and tests mmap by allocating a physical page.\n",
        0
    },
    {
        "virgl_test", cmd_virgl_test,
        "Test VirtIO GPU 3D (virgl) pipeline",
        "virgl_test: virgl_test\n"
        "    Create virgl context, clear render target, verify pixels.\n",
        "NAME\n"
        "    virgl_test - test VirtIO GPU 3D pipeline\n\n"
        "SYNOPSIS\n"
        "    virgl_test\n\n"
        "DESCRIPTION\n"
        "    Validates the virgl 3D command pipeline by creating a context,\n"
        "    encoding Gallium clear commands, and verifying pixel output.\n"
        "    Requires QEMU with -vga virtio -display sdl,gl=on.\n",
        0
    },
    {
        "gfxbench", cmd_gfxbench,
        "Run graphics rendering stress test",
        "gfxbench: gfxbench\n"
        "    Stress test the rendering pipeline at max throughput.\n"
        "    Press 'q' to quit early.\n",
        "NAME\n"
        "    gfxbench - graphics rendering stress test\n\n"
        "SYNOPSIS\n"
        "    gfxbench\n\n"
        "DESCRIPTION\n"
        "    Runs five stress phases with no frame cap: rect flood,\n"
        "    line storm, circle cascade, alpha blending, and combined\n"
        "    chaos. Each phase runs for 5 seconds. FPS is measured and\n"
        "    printed as a summary at the end. Press 'q' or ESC to quit.\n",
        CMD_FLAG_ROOT
    },
    {
        "fps", cmd_fps,
        "Toggle FPS overlay on screen",
        "fps: fps\n"
        "    Toggle a live FPS counter in the top-right corner of the desktop.\n",
        "NAME\n"
        "    fps - toggle FPS overlay\n\n"
        "SYNOPSIS\n"
        "    fps\n\n"
        "DESCRIPTION\n"
        "    Toggles a persistent FPS counter overlay on the top-right\n"
        "    corner of the desktop. The counter updates every second and\n"
        "    shows the number of WM composites per second. Run 'fps'\n"
        "    again to turn it off.\n",
        CMD_FLAG_ROOT
    },
    {
        "imgview", cmd_imgview,
        "Display an image file (BMP/PNG)",
        "imgview: imgview <path>\n"
        "    Load and display an image file fullscreen.\n"
        "    Supports BMP (24/32bpp) and PNG (8-bit RGB/RGBA).\n"
        "    Press any key to exit.\n",
        "NAME\n"
        "    imgview - image viewer\n\n"
        "SYNOPSIS\n"
        "    imgview <path>\n\n"
        "DESCRIPTION\n"
        "    Loads a BMP or PNG image from the filesystem and displays\n"
        "    it centered on screen with a black background. The image\n"
        "    is scaled to fit while preserving aspect ratio.\n"
        "    Press any key to return to the shell.\n",
        CMD_FLAG_ROOT
    },
};

const command_t *cmd_gfx_commands(int *count) {
    *count = sizeof(gfx_commands) / sizeof(gfx_commands[0]);
    return gfx_commands;
}
