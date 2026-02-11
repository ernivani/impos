#include <kernel/gfx_path.h>
#include <stdlib.h>
#include <string.h>

/* ═══ Path construction ══════════════════════════════════════ */

#define PATH_INITIAL_CAP 64

void gfx_path_init(gfx_path_t *p) {
    p->cmds = (gfx_path_cmd_t *)malloc(PATH_INITIAL_CAP * sizeof(gfx_path_cmd_t));
    p->count = 0;
    p->capacity = p->cmds ? PATH_INITIAL_CAP : 0;
}

void gfx_path_free(gfx_path_t *p) {
    if (p->cmds) free(p->cmds);
    p->cmds = 0;
    p->count = p->capacity = 0;
}

void gfx_path_reset(gfx_path_t *p) {
    p->count = 0;
}

static void path_push(gfx_path_t *p, gfx_path_cmd_t *cmd) {
    if (p->count >= p->capacity) {
        int new_cap = p->capacity ? p->capacity * 2 : PATH_INITIAL_CAP;
        gfx_path_cmd_t *n = (gfx_path_cmd_t *)realloc(p->cmds,
                              (size_t)new_cap * sizeof(gfx_path_cmd_t));
        if (!n) return;
        p->cmds = n;
        p->capacity = new_cap;
    }
    p->cmds[p->count++] = *cmd;
}

void gfx_path_move_to(gfx_path_t *p, fix26_6 x, fix26_6 y) {
    gfx_path_cmd_t c = { PATH_CMD_MOVE, x, y, 0, 0 };
    path_push(p, &c);
}

void gfx_path_line_to(gfx_path_t *p, fix26_6 x, fix26_6 y) {
    gfx_path_cmd_t c = { PATH_CMD_LINE, x, y, 0, 0 };
    path_push(p, &c);
}

void gfx_path_quad_to(gfx_path_t *p, fix26_6 cx, fix26_6 cy,
                       fix26_6 x, fix26_6 y) {
    gfx_path_cmd_t c = { PATH_CMD_QUAD, x, y, cx, cy };
    path_push(p, &c);
}

void gfx_path_close(gfx_path_t *p) {
    gfx_path_cmd_t c = { PATH_CMD_CLOSE, 0, 0, 0, 0 };
    path_push(p, &c);
}

/* ═══ Convenience shapes ═════════════════════════════════════ */

void gfx_path_rect(gfx_path_t *p, fix26_6 x, fix26_6 y,
                    fix26_6 w, fix26_6 h) {
    gfx_path_move_to(p, x, y);
    gfx_path_line_to(p, x + w, y);
    gfx_path_line_to(p, x + w, y + h);
    gfx_path_line_to(p, x, y + h);
    gfx_path_close(p);
}

void gfx_path_rounded_rect(gfx_path_t *p, fix26_6 x, fix26_6 y,
                             fix26_6 w, fix26_6 h, fix26_6 r) {
    if (r <= 0) { gfx_path_rect(p, x, y, w, h); return; }

    gfx_path_move_to(p, x + r, y);
    gfx_path_line_to(p, x + w - r, y);
    gfx_path_quad_to(p, x + w, y, x + w, y + r);
    gfx_path_line_to(p, x + w, y + h - r);
    gfx_path_quad_to(p, x + w, y + h, x + w - r, y + h);
    gfx_path_line_to(p, x + r, y + h);
    gfx_path_quad_to(p, x, y + h, x, y + h - r);
    gfx_path_line_to(p, x, y + r);
    gfx_path_quad_to(p, x, y, x + r, y);
    gfx_path_close(p);
}

void gfx_path_ellipse(gfx_path_t *p, fix26_6 cx, fix26_6 cy,
                       fix26_6 rx, fix26_6 ry) {
    /* Approximate with 8 quadratic Bezier curves (2 per quadrant).
       For quadratic Bezier: control offset ≈ r * 4/3 * tan(pi/8) ≈ r * 0.5522
       Using 36/65 ≈ 0.5538, close enough */
    fix26_6 kx = FIX26_6_MUL(rx, FIX26_6_FRAC(36, 65));
    fix26_6 ky = FIX26_6_MUL(ry, FIX26_6_FRAC(36, 65));

    gfx_path_move_to(p, cx + rx, cy);
    gfx_path_quad_to(p, cx + rx, cy + ky, cx + kx, cy + ry);
    gfx_path_line_to(p, cx, cy + ry);
    gfx_path_quad_to(p, cx - kx, cy + ry, cx - rx, cy + ky);
    gfx_path_line_to(p, cx - rx, cy);
    gfx_path_quad_to(p, cx - rx, cy - ky, cx - kx, cy - ry);
    gfx_path_line_to(p, cx, cy - ry);
    gfx_path_quad_to(p, cx + kx, cy - ry, cx + rx, cy - ky);
    gfx_path_line_to(p, cx + rx, cy);
    gfx_path_close(p);
}

void gfx_path_circle(gfx_path_t *p, fix26_6 cx, fix26_6 cy, fix26_6 r) {
    gfx_path_ellipse(p, cx, cy, r, r);
}

/* ═══ Edge-based scanline rasterizer ═════════════════════════ */

typedef struct {
    fix26_6 x_top;     /* x at y_min (immutable after build) */
    fix26_6 dx;        /* x increment per scanline unit (26.6) */
    int y_min, y_max;  /* scanline range [y_min, y_max) */
    int winding;       /* +1 or -1 */
} raster_edge_t;

#define MAX_EDGES 2048
#define MAX_FLAT_DEPTH 8

/* Flatten quadratic Bezier into line segments via De Casteljau */
static void flatten_quad(gfx_path_t *flat,
                          fix26_6 x0, fix26_6 y0,
                          fix26_6 cx, fix26_6 cy,
                          fix26_6 x1, fix26_6 y1,
                          int depth) {
    if (depth >= MAX_FLAT_DEPTH) {
        gfx_path_line_to(flat, x1, y1);
        return;
    }

    /* Flatness test: distance from control point to chord midpoint */
    fix26_6 mx = (x0 + x1) >> 1;
    fix26_6 my = (y0 + y1) >> 1;
    fix26_6 ddx = cx - mx;
    fix26_6 ddy = cy - my;

    /* If deviation < 1 pixel (64 in 26.6), emit line */
    int64_t dev2 = (int64_t)ddx * ddx + (int64_t)ddy * ddy;
    if (dev2 < (int64_t)64 * 64) {
        gfx_path_line_to(flat, x1, y1);
        return;
    }

    /* De Casteljau subdivision at t=0.5 */
    fix26_6 ax = (x0 + cx) >> 1;
    fix26_6 ay = (y0 + cy) >> 1;
    fix26_6 bx = (cx + x1) >> 1;
    fix26_6 by = (cy + y1) >> 1;
    fix26_6 px = (ax + bx) >> 1;
    fix26_6 py = (ay + by) >> 1;

    flatten_quad(flat, x0, y0, ax, ay, px, py, depth + 1);
    flatten_quad(flat, px, py, bx, by, x1, y1, depth + 1);
}

/* Flatten path: convert all QUAD commands to LINE segments */
static void flatten_path(gfx_path_t *src, gfx_path_t *dst) {
    fix26_6 cur_x = 0, cur_y = 0;

    for (int i = 0; i < src->count; i++) {
        gfx_path_cmd_t *c = &src->cmds[i];
        switch (c->cmd) {
        case PATH_CMD_MOVE:
            gfx_path_move_to(dst, c->x, c->y);
            cur_x = c->x; cur_y = c->y;
            break;
        case PATH_CMD_LINE:
            gfx_path_line_to(dst, c->x, c->y);
            cur_x = c->x; cur_y = c->y;
            break;
        case PATH_CMD_QUAD:
            flatten_quad(dst, cur_x, cur_y, c->cx, c->cy, c->x, c->y, 0);
            cur_x = c->x; cur_y = c->y;
            break;
        case PATH_CMD_CLOSE:
            gfx_path_close(dst);
            break;
        }
    }
}

/* Build edges from flattened path.
   sub_scale: 1 for non-AA, AA_SUPERSAMPLE for AA.
   All y values and dx are in sub-scanline units. */
static int build_edges(gfx_path_t *flat, raster_edge_t *edges, int max_edges,
                        int sub_scale) {
    int n = 0;
    fix26_6 cur_x = 0, cur_y = 0;
    fix26_6 sub_x = 0, sub_y = 0;

    for (int i = 0; i < flat->count && n < max_edges; i++) {
        gfx_path_cmd_t *c = &flat->cmds[i];
        fix26_6 x0, y0, x1, y1;

        switch (c->cmd) {
        case PATH_CMD_MOVE:
            sub_x = c->x; sub_y = c->y;
            cur_x = c->x; cur_y = c->y;
            continue;
        case PATH_CMD_LINE:
            x0 = cur_x; y0 = cur_y;
            x1 = c->x;  y1 = c->y;
            cur_x = x1;  cur_y = y1;
            break;
        case PATH_CMD_CLOSE:
            x0 = cur_x; y0 = cur_y;
            x1 = sub_x; y1 = sub_y;
            cur_x = sub_x; cur_y = sub_y;
            break;
        default:
            continue;
        }

        /* Convert y to sub-scanline coordinates (integer scanline index).
           Coordinates are in 26.6 fixed point; convert to pixel then scale. */
        int sy0 = FIX26_6_FLOOR(y0) * sub_scale;
        int sy1 = FIX26_6_FLOOR(y1) * sub_scale;

        /* Also account for fractional y within the pixel */
        sy0 += (int)((((uint32_t)(y0) & 63) * sub_scale) >> 6);
        sy1 += (int)((((uint32_t)(y1) & 63) * sub_scale) >> 6);

        if (sy0 == sy1) continue; /* horizontal — skip */

        raster_edge_t *e = &edges[n];
        if (sy0 < sy1) {
            e->y_min = sy0;
            e->y_max = sy1;
            e->winding = 1;
            e->x_top = x0;
            int span = sy1 - sy0;
            e->dx = (fix26_6)(((int64_t)(x1 - x0) << 6) / ((int64_t)span << 6));
            /* Simplify: dx = (x1 - x0) / span, but both in 26.6 already */
            e->dx = (fix26_6)((int64_t)(x1 - x0) / span);
        } else {
            e->y_min = sy1;
            e->y_max = sy0;
            e->winding = -1;
            e->x_top = x1;
            int span = sy0 - sy1;
            e->dx = (fix26_6)((int64_t)(x0 - x1) / span);
        }
        n++;
    }
    return n;
}

/* Sort active edge indices by their x position at current scanline */
static void sort_by_x(int *arr, int count, fix26_6 *x_vals) {
    for (int i = 1; i < count; i++) {
        int key = arr[i];
        fix26_6 kx = x_vals[key];
        int j = i - 1;
        while (j >= 0 && x_vals[arr[j]] > kx) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/* ═══ Non-AA fill (no supersampling) ═════════════════════════ */

void gfx_surf_fill_path(gfx_surface_t *s, gfx_path_t *p, uint32_t color) {
    gfx_path_t flat;
    gfx_path_init(&flat);
    flatten_path(p, &flat);

    raster_edge_t *edges = (raster_edge_t *)malloc(MAX_EDGES * sizeof(raster_edge_t));
    if (!edges) { gfx_path_free(&flat); return; }

    int nedges = build_edges(&flat, edges, MAX_EDGES, 1);
    gfx_path_free(&flat);
    if (nedges == 0) { free(edges); return; }

    /* Find y range */
    int y_min = edges[0].y_min, y_max = edges[0].y_max;
    for (int i = 1; i < nedges; i++) {
        if (edges[i].y_min < y_min) y_min = edges[i].y_min;
        if (edges[i].y_max > y_max) y_max = edges[i].y_max;
    }
    if (y_min < 0) y_min = 0;
    if (y_max > s->h) y_max = s->h;

    int *active = (int *)malloc((size_t)nedges * sizeof(int));
    fix26_6 *cur_x = (fix26_6 *)malloc((size_t)nedges * sizeof(fix26_6));
    if (!active || !cur_x) {
        if (active) free(active);
        if (cur_x) free(cur_x);
        free(edges);
        return;
    }

    for (int y = y_min; y < y_max; y++) {
        /* Collect active edges and compute x at this scanline */
        int na = 0;
        for (int i = 0; i < nedges; i++) {
            if (y >= edges[i].y_min && y < edges[i].y_max) {
                int dy = y - edges[i].y_min;
                cur_x[i] = edges[i].x_top + edges[i].dx * dy;
                active[na++] = i;
            }
        }
        if (na == 0) continue;

        sort_by_x(active, na, cur_x);

        /* Fill spans using non-zero winding rule */
        int winding = 0;
        int span_start = 0;
        for (int i = 0; i < na; i++) {
            int prev_winding = winding;
            winding += edges[active[i]].winding;

            if (prev_winding == 0 && winding != 0) {
                span_start = FIX26_6_ROUND(cur_x[active[i]]);
            } else if (prev_winding != 0 && winding == 0) {
                int span_end = FIX26_6_ROUND(cur_x[active[i]]);
                if (span_start < 0) span_start = 0;
                if (span_end > s->w) span_end = s->w;
                if (y >= 0 && y < s->h) {
                    uint32_t *row = s->buf + y * s->pitch;
                    for (int x = span_start; x < span_end; x++)
                        row[x] = color;
                }
            }
        }
    }

    free(cur_x);
    free(active);
    free(edges);
}

/* ═══ AA fill (4x vertical supersampling) ════════════════════ */

#define AA_SUPERSAMPLE 4

void gfx_surf_fill_path_aa(gfx_surface_t *s, gfx_path_t *p, uint32_t color) {
    gfx_path_t flat;
    gfx_path_init(&flat);
    flatten_path(p, &flat);

    raster_edge_t *edges = (raster_edge_t *)malloc(MAX_EDGES * sizeof(raster_edge_t));
    if (!edges) { gfx_path_free(&flat); return; }

    int nedges = build_edges(&flat, edges, MAX_EDGES, AA_SUPERSAMPLE);
    gfx_path_free(&flat);
    if (nedges == 0) { free(edges); return; }

    /* Find y range in sub-pixel units */
    int sy_min = edges[0].y_min, sy_max = edges[0].y_max;
    for (int i = 1; i < nedges; i++) {
        if (edges[i].y_min < sy_min) sy_min = edges[i].y_min;
        if (edges[i].y_max > sy_max) sy_max = edges[i].y_max;
    }

    /* Pixel row range */
    int py_min = sy_min / AA_SUPERSAMPLE;
    int py_max = (sy_max + AA_SUPERSAMPLE - 1) / AA_SUPERSAMPLE;
    if (py_min < 0) py_min = 0;
    if (py_max > s->h) py_max = s->h;

    int cov_w = s->w;
    uint8_t *coverage = (uint8_t *)malloc((size_t)cov_w);
    int *active = (int *)malloc((size_t)nedges * sizeof(int));
    fix26_6 *edge_x = (fix26_6 *)malloc((size_t)nedges * sizeof(fix26_6));
    if (!coverage || !active || !edge_x) {
        if (coverage) free(coverage);
        if (active) free(active);
        if (edge_x) free(edge_x);
        free(edges);
        return;
    }

    uint32_t cr = (color >> 16) & 0xFF;
    uint32_t cg = (color >> 8) & 0xFF;
    uint32_t cb = color & 0xFF;

    for (int py = py_min; py < py_max; py++) {
        memset(coverage, 0, (size_t)cov_w);

        for (int sub = 0; sub < AA_SUPERSAMPLE; sub++) {
            int sy = py * AA_SUPERSAMPLE + sub;

            /* Collect active edges and compute x at this sub-scanline */
            int na = 0;
            for (int i = 0; i < nedges; i++) {
                if (sy >= edges[i].y_min && sy < edges[i].y_max) {
                    int dy = sy - edges[i].y_min;
                    edge_x[i] = edges[i].x_top + edges[i].dx * dy;
                    active[na++] = i;
                }
            }
            if (na == 0) continue;

            sort_by_x(active, na, edge_x);

            /* Fill spans with non-zero winding */
            int winding = 0;
            int span_start = 0;
            for (int i = 0; i < na; i++) {
                int prev_winding = winding;
                winding += edges[active[i]].winding;

                if (prev_winding == 0 && winding != 0) {
                    span_start = FIX26_6_ROUND(edge_x[active[i]]);
                } else if (prev_winding != 0 && winding == 0) {
                    int span_end = FIX26_6_ROUND(edge_x[active[i]]);
                    if (span_start < 0) span_start = 0;
                    if (span_end > cov_w) span_end = cov_w;
                    for (int x = span_start; x < span_end; x++)
                        coverage[x]++;
                }
            }
        }

        /* Blend coverage into surface */
        uint32_t *row = s->buf + py * s->pitch;
        for (int x = 0; x < cov_w; x++) {
            if (coverage[x] == 0) continue;
            uint32_t alpha = (uint32_t)coverage[x] * 255 / AA_SUPERSAMPLE;
            if (alpha >= 255) {
                row[x] = color;
            } else {
                uint32_t inv = 255 - alpha;
                uint32_t dp = row[x];
                uint32_t dr = (dp >> 16) & 0xFF;
                uint32_t dg = (dp >> 8) & 0xFF;
                uint32_t db = dp & 0xFF;
                uint32_t or_ = (cr * alpha + dr * inv) / 255;
                uint32_t og = (cg * alpha + dg * inv) / 255;
                uint32_t ob = (cb * alpha + db * inv) / 255;
                row[x] = (or_ << 16) | (og << 8) | ob;
            }
        }
    }

    free(edge_x);
    free(active);
    free(coverage);
    free(edges);
}

/* ═══ Stroke (expand to outline, then fill) ═════════════════ */

void gfx_surf_stroke_path(gfx_surface_t *s, gfx_path_t *p,
                            uint32_t color, fix26_6 width) {
    fix26_6 half = width >> 1;
    fix26_6 px_cur = 0, py_cur = 0;
    fix26_6 sub_x = 0, sub_y = 0;

    for (int i = 0; i < p->count; i++) {
        gfx_path_cmd_t *c = &p->cmds[i];
        fix26_6 x1, y1;

        if (c->cmd == PATH_CMD_MOVE) {
            sub_x = c->x; sub_y = c->y;
            px_cur = c->x; py_cur = c->y;
            continue;
        }
        if (c->cmd == PATH_CMD_CLOSE) {
            x1 = sub_x; y1 = sub_y;
        } else if (c->cmd == PATH_CMD_LINE) {
            x1 = c->x; y1 = c->y;
        } else {
            continue;
        }

        fix26_6 dx = x1 - px_cur;
        fix26_6 dy = y1 - py_cur;
        if (dx == 0 && dy == 0) { px_cur = x1; py_cur = y1; continue; }

        /* Perpendicular normal: (-dy, dx), scaled to half-width.
           Approximate length to avoid sqrt. */
        fix26_6 nx, ny;
        if (dx == 0) {
            nx = (dy > 0) ? half : -half;
            ny = 0;
        } else if (dy == 0) {
            nx = 0;
            ny = (dx > 0) ? -half : half;
        } else {
            /* Use the dominant axis for a rough approximation */
            int64_t adx = dx < 0 ? -dx : dx;
            int64_t ady = dy < 0 ? -dy : dy;
            int64_t len = adx > ady ? adx + (ady >> 1) : ady + (adx >> 1);
            if (len == 0) len = 1;
            nx = (fix26_6)(-(int64_t)dy * half / len);
            ny = (fix26_6)((int64_t)dx * half / len);
        }

        gfx_path_t seg;
        gfx_path_init(&seg);
        gfx_path_move_to(&seg, px_cur + nx, py_cur + ny);
        gfx_path_line_to(&seg, x1 + nx, y1 + ny);
        gfx_path_line_to(&seg, x1 - nx, y1 - ny);
        gfx_path_line_to(&seg, px_cur - nx, py_cur - ny);
        gfx_path_close(&seg);
        gfx_surf_fill_path_aa(s, &seg, color);
        gfx_path_free(&seg);

        px_cur = x1; py_cur = y1;
    }
}

/* ═══ Backbuffer convenience wrappers ════════════════════════ */

void gfx_fill_path(gfx_path_t *p, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_fill_path(&s, p, color);
}

void gfx_fill_path_aa(gfx_path_t *p, uint32_t color) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_fill_path_aa(&s, p, color);
}

void gfx_stroke_path(gfx_path_t *p, uint32_t color, fix26_6 width) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_stroke_path(&s, p, color, width);
}
