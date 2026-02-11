#ifndef _KERNEL_GFX_PATH_H
#define _KERNEL_GFX_PATH_H

#include <stdint.h>
#include <kernel/gfx.h>

/* ═══ 26.6 fixed-point math ═══════════════════════════════════ */

typedef int32_t fix26_6;

#define FIX26_6(x)        ((fix26_6)((x) << 6))
#define FIX26_6_FRAC(x,d) ((fix26_6)(((x) << 6) / (d)))
#define FIX26_6_ROUND(x)  ((int)(((x) + 32) >> 6))
#define FIX26_6_FLOOR(x)  ((int)((x) >> 6))
#define FIX26_6_CEIL(x)   ((int)(((x) + 63) >> 6))
#define FIX26_6_MUL(a,b)  ((fix26_6)(((int64_t)(a) * (b)) >> 6))
#define FIX26_6_DIV(a,b)  ((fix26_6)(((int64_t)(a) << 6) / (b)))

/* ═══ Path commands ═══════════════════════════════════════════ */

#define PATH_CMD_MOVE   0
#define PATH_CMD_LINE   1
#define PATH_CMD_QUAD   2
#define PATH_CMD_CLOSE  3

typedef struct {
    uint8_t cmd;
    fix26_6 x, y;       /* endpoint */
    fix26_6 cx, cy;     /* control point (QUAD only) */
} gfx_path_cmd_t;

typedef struct {
    gfx_path_cmd_t *cmds;
    int count;
    int capacity;
} gfx_path_t;

/* ═══ Path construction ══════════════════════════════════════ */

void gfx_path_init(gfx_path_t *p);
void gfx_path_free(gfx_path_t *p);
void gfx_path_reset(gfx_path_t *p);

void gfx_path_move_to(gfx_path_t *p, fix26_6 x, fix26_6 y);
void gfx_path_line_to(gfx_path_t *p, fix26_6 x, fix26_6 y);
void gfx_path_quad_to(gfx_path_t *p, fix26_6 cx, fix26_6 cy, fix26_6 x, fix26_6 y);
void gfx_path_close(gfx_path_t *p);

/* Convenience shapes (coordinates in 26.6 fixed-point) */
void gfx_path_rect(gfx_path_t *p, fix26_6 x, fix26_6 y, fix26_6 w, fix26_6 h);
void gfx_path_rounded_rect(gfx_path_t *p, fix26_6 x, fix26_6 y,
                            fix26_6 w, fix26_6 h, fix26_6 r);
void gfx_path_ellipse(gfx_path_t *p, fix26_6 cx, fix26_6 cy,
                       fix26_6 rx, fix26_6 ry);
void gfx_path_circle(gfx_path_t *p, fix26_6 cx, fix26_6 cy, fix26_6 r);

/* ═══ Rasterization (surface-targeted) ═══════════════════════ */

void gfx_surf_fill_path(gfx_surface_t *s, gfx_path_t *p, uint32_t color);
void gfx_surf_fill_path_aa(gfx_surface_t *s, gfx_path_t *p, uint32_t color);
void gfx_surf_stroke_path(gfx_surface_t *s, gfx_path_t *p,
                           uint32_t color, fix26_6 width);

/* ═══ Rasterization (backbuffer convenience) ═════════════════ */

void gfx_fill_path(gfx_path_t *p, uint32_t color);
void gfx_fill_path_aa(gfx_path_t *p, uint32_t color);
void gfx_stroke_path(gfx_path_t *p, uint32_t color, fix26_6 width);

#endif
