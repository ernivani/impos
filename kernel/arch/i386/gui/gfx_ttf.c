#include <kernel/gfx_ttf.h>
#include <kernel/gfx_path.h>
#include <kernel/gfx.h>
#include <stdlib.h>
#include <string.h>

#include "font8x16.h"

/* ═══ Built-in vector font (auto-traced from font8x16) ══════ */

/* Each glyph rectangle: pixel position and size in 8x16 grid */
typedef struct { uint8_t x, y, w, h; } glyph_rect_t;

#define MAX_BUILTIN_RECTS 4096
static glyph_rect_t builtin_rects[MAX_BUILTIN_RECTS];
static uint16_t builtin_rect_start[257]; /* index per glyph + sentinel */
static int builtin_total_rects = 0;
static int builtin_font_ready = 0;

void gfx_builtin_font_init(void) {
    int total = 0;

    for (int ch = 0; ch < 256; ch++) {
        builtin_rect_start[ch] = (uint16_t)total;
        const uint8_t *glyph = font8x16[ch];

        /* Track which pixels are already claimed by a rectangle */
        uint8_t used[16]; /* one byte per row (8 bits) */
        memset(used, 0, sizeof(used));

        /* Scan for horizontal runs, then merge vertically */
        for (int row = 0; row < 16; row++) {
            uint8_t bits = glyph[row];
            int col = 0;
            while (col < 8) {
                /* Skip OFF or already-used pixels */
                if (!(bits & (0x80 >> col)) || (used[row] & (0x80 >> col))) {
                    col++;
                    continue;
                }
                /* Found start of a run */
                int run_start = col;
                while (col < 8 && (bits & (0x80 >> col)) &&
                       !(used[row] & (0x80 >> col)))
                    col++;
                int run_w = col - run_start;

                /* Try to extend this run downward */
                int run_h = 1;
                for (int r2 = row + 1; r2 < 16; r2++) {
                    uint8_t b2 = glyph[r2];
                    int ok = 1;
                    for (int c2 = run_start; c2 < run_start + run_w; c2++) {
                        if (!(b2 & (0x80 >> c2)) || (used[r2] & (0x80 >> c2))) {
                            ok = 0;
                            break;
                        }
                    }
                    if (!ok) break;
                    run_h++;
                }

                /* Mark pixels as used */
                for (int r2 = row; r2 < row + run_h; r2++)
                    for (int c2 = run_start; c2 < run_start + run_w; c2++)
                        used[r2] |= (0x80 >> c2);

                /* Store rectangle */
                if (total < MAX_BUILTIN_RECTS) {
                    builtin_rects[total].x = (uint8_t)run_start;
                    builtin_rects[total].y = (uint8_t)row;
                    builtin_rects[total].w = (uint8_t)run_w;
                    builtin_rects[total].h = (uint8_t)run_h;
                    total++;
                }
            }
        }
    }

    builtin_rect_start[256] = (uint16_t)total;
    builtin_total_rects = total;
    builtin_font_ready = 1;
}

/* Render one character using the built-in vector font */
void gfx_surf_draw_char_vec(gfx_surface_t *s, int x, int y,
                              char c, uint32_t color, int size_px) {
    if (!builtin_font_ready) return;
    unsigned char uc = (unsigned char)c;

    int start = builtin_rect_start[uc];
    int end = builtin_rect_start[uc + 1];
    if (start == end) return;

    /* Scale factor: size_px / 16, in 26.6 fixed-point */
    fix26_6 scale = FIX26_6_FRAC(size_px, 16);

    gfx_path_t path;
    gfx_path_init(&path);

    for (int i = start; i < end; i++) {
        glyph_rect_t *r = &builtin_rects[i];
        fix26_6 rx = FIX26_6(x) + FIX26_6_MUL(FIX26_6(r->x), scale);
        fix26_6 ry = FIX26_6(y) + FIX26_6_MUL(FIX26_6(r->y), scale);
        fix26_6 rw = FIX26_6_MUL(FIX26_6(r->w), scale);
        fix26_6 rh = FIX26_6_MUL(FIX26_6(r->h), scale);
        gfx_path_rect(&path, rx, ry, rw, rh);
    }

    gfx_surf_fill_path_aa(s, &path, color);
    gfx_path_free(&path);
}

void gfx_surf_draw_string_vec(gfx_surface_t *s, int x, int y,
                                const char *str, uint32_t color, int size_px) {
    int advance = size_px / 2; /* 8/16 ratio = 0.5 for monospace */
    if (advance < 1) advance = 1;
    while (*str) {
        gfx_surf_draw_char_vec(s, x, y, *str, color, size_px);
        x += advance;
        str++;
    }
}

void gfx_draw_char_vec(int x, int y, char c, uint32_t color, int size_px) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_char_vec(&s, x, y, c, color, size_px);
}

void gfx_draw_string_vec(int x, int y, const char *str,
                           uint32_t color, int size_px) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_string_vec(&s, x, y, str, color, size_px);
}

int gfx_string_vec_width(const char *str, int size_px) {
    int advance = size_px / 2;
    if (advance < 1) advance = 1;
    return (int)strlen(str) * advance;
}

/* ═══ TTF byte reading helpers ═══════════════════════════════ */

static inline uint16_t read_u16(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int16_t read_i16(const uint8_t *p) {
    return (int16_t)((p[0] << 8) | p[1]);
}

static inline uint32_t read_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

/* ═══ TTF loading ════════════════════════════════════════════ */

static uint32_t ttf_find_table(const uint8_t *data, uint32_t len,
                                const char *tag) {
    if (len < 12) return 0;
    uint16_t num_tables = read_u16(data + 4);
    uint32_t off = 12;
    for (uint16_t i = 0; i < num_tables && off + 16 <= len; i++, off += 16) {
        if (data[off] == tag[0] && data[off+1] == tag[1] &&
            data[off+2] == tag[2] && data[off+3] == tag[3]) {
            return read_u32(data + off + 8); /* table offset */
        }
    }
    return 0;
}

int ttf_load(ttf_font_t *font, const uint8_t *data, uint32_t len) {
    memset(font, 0, sizeof(*font));
    if (len < 12) return -1;

    uint32_t sf_ver = read_u32(data);
    if (sf_ver != 0x00010000 && sf_ver != 0x74727565) return -1;

    font->data = data;
    font->data_len = len;

    /* Find required tables */
    font->off_head = ttf_find_table(data, len, "head");
    font->off_maxp = ttf_find_table(data, len, "maxp");
    font->off_cmap = ttf_find_table(data, len, "cmap");
    font->off_loca = ttf_find_table(data, len, "loca");
    font->off_glyf = ttf_find_table(data, len, "glyf");
    font->off_hhea = ttf_find_table(data, len, "hhea");
    font->off_hmtx = ttf_find_table(data, len, "hmtx");

    if (!font->off_head || !font->off_maxp || !font->off_cmap ||
        !font->off_loca || !font->off_glyf || !font->off_hhea ||
        !font->off_hmtx)
        return -1;

    /* Parse head */
    if (font->off_head + 54 > len) return -1;
    font->units_per_em = read_u16(data + font->off_head + 18);
    font->index_to_loc_fmt = read_i16(data + font->off_head + 50);
    if (font->units_per_em == 0) return -1;

    /* Parse maxp */
    if (font->off_maxp + 6 > len) return -1;
    font->num_glyphs = read_u16(data + font->off_maxp + 4);

    /* Parse hhea */
    if (font->off_hhea + 36 > len) return -1;
    font->ascender = read_i16(data + font->off_hhea + 4);
    font->descender = read_i16(data + font->off_hhea + 6);
    font->line_gap = read_i16(data + font->off_hhea + 8);
    font->num_h_metrics = read_u16(data + font->off_hhea + 34);

    /* Find cmap subtable */
    if (font->off_cmap + 4 > len) return -1;
    uint16_t num_subtables = read_u16(data + font->off_cmap + 2);
    uint32_t cmap_base = font->off_cmap;

    for (uint16_t i = 0; i < num_subtables; i++) {
        uint32_t rec = cmap_base + 4 + i * 8;
        if (rec + 8 > len) break;
        uint16_t platform = read_u16(data + rec);
        uint16_t encoding = read_u16(data + rec + 2);
        uint32_t sub_off = read_u32(data + rec + 4);
        uint32_t abs_off = cmap_base + sub_off;
        if (abs_off + 2 > len) continue;
        uint16_t fmt = read_u16(data + abs_off);

        if (platform == 3 && encoding == 1 && fmt == 4) {
            font->cmap_fmt4_off = abs_off;
        } else if (platform == 1 && encoding == 0 && fmt == 0) {
            font->cmap_fmt0_off = abs_off;
        }
    }

    if (!font->cmap_fmt4_off && !font->cmap_fmt0_off) return -1;

    font->cache_size_px = 0;
    return 0;
}

void ttf_free(ttf_font_t *font) {
    for (int i = 0; i < TTF_CACHE_SIZE; i++) {
        if (font->cache[i].alpha) {
            free(font->cache[i].alpha);
            font->cache[i].alpha = 0;
        }
    }
    font->cache_size_px = 0;
}

/* ═══ Character mapping ══════════════════════════════════════ */

static uint16_t cmap_fmt4_lookup(const uint8_t *data, uint32_t len,
                                   uint32_t off, uint16_t cp) {
    if (off + 14 > len) return 0;
    uint16_t seg_count2 = read_u16(data + off + 6);
    uint16_t seg_count = seg_count2 / 2;
    if (seg_count == 0) return 0;

    uint32_t end_codes   = off + 14;
    uint32_t start_codes = end_codes + seg_count2 + 2;
    uint32_t id_delta    = start_codes + seg_count2;
    uint32_t id_range    = id_delta + seg_count2;

    for (uint16_t i = 0; i < seg_count; i++) {
        uint32_t ei = end_codes + i * 2;
        uint32_t si = start_codes + i * 2;
        uint32_t di = id_delta + i * 2;
        uint32_t ri = id_range + i * 2;
        if (ri + 2 > len) break;

        uint16_t end_code = read_u16(data + ei);
        if (cp > end_code) continue;

        uint16_t start_code = read_u16(data + si);
        if (cp < start_code) return 0;

        uint16_t delta = read_u16(data + di);
        uint16_t range = read_u16(data + ri);

        if (range == 0) {
            return (uint16_t)(cp + delta);
        } else {
            uint32_t idx_off = ri + range + (cp - start_code) * 2;
            if (idx_off + 2 > len) return 0;
            uint16_t glyph_id = read_u16(data + idx_off);
            if (glyph_id == 0) return 0;
            return (uint16_t)(glyph_id + delta);
        }
    }
    return 0;
}

uint16_t ttf_char_to_glyph(ttf_font_t *font, uint16_t codepoint) {
    if (font->cmap_fmt4_off) {
        uint16_t g = cmap_fmt4_lookup(font->data, font->data_len,
                                       font->cmap_fmt4_off, codepoint);
        if (g) return g;
    }
    if (font->cmap_fmt0_off && codepoint < 256) {
        uint32_t off = font->cmap_fmt0_off + 6 + codepoint;
        if (off < font->data_len)
            return font->data[off];
    }
    return 0;
}

/* ═══ Glyph metrics ══════════════════════════════════════════ */

int ttf_glyph_advance(ttf_font_t *font, uint16_t glyph_id) {
    if (glyph_id < font->num_h_metrics) {
        uint32_t off = font->off_hmtx + glyph_id * 4;
        if (off + 2 <= font->data_len)
            return read_u16(font->data + off);
    } else if (font->num_h_metrics > 0) {
        uint32_t off = font->off_hmtx + (font->num_h_metrics - 1) * 4;
        if (off + 2 <= font->data_len)
            return read_u16(font->data + off);
    }
    return font->units_per_em / 2;
}

/* ═══ Glyph outline decoding ═════════════════════════════════ */

static uint32_t glyf_offset(ttf_font_t *font, uint16_t glyph_id) {
    if (font->index_to_loc_fmt == 0) {
        /* Short format: uint16 × 2 */
        uint32_t off = font->off_loca + glyph_id * 2;
        if (off + 2 > font->data_len) return 0;
        return read_u16(font->data + off) * 2;
    } else {
        /* Long format: uint32 */
        uint32_t off = font->off_loca + glyph_id * 4;
        if (off + 4 > font->data_len) return 0;
        return read_u32(font->data + off);
    }
}

/* Decode a simple glyph (numberOfContours >= 0) into a path */
static int decode_simple_glyph(ttf_font_t *font, uint32_t glyf_off,
                                 int num_contours, gfx_path_t *path) {
    const uint8_t *data = font->data;
    uint32_t len = font->data_len;
    uint32_t p = glyf_off + 10; /* skip header (10 bytes) */

    if (num_contours <= 0) return 0;

    /* Read endPtsOfContours */
    uint16_t *end_pts = (uint16_t *)malloc((size_t)num_contours * 2);
    if (!end_pts) return -1;

    for (int i = 0; i < num_contours; i++) {
        if (p + 2 > len) { free(end_pts); return -1; }
        end_pts[i] = read_u16(data + p);
        p += 2;
    }

    int num_points = end_pts[num_contours - 1] + 1;
    if (num_points > 4096) { free(end_pts); return -1; }

    /* Skip instructions */
    if (p + 2 > len) { free(end_pts); return -1; }
    uint16_t inst_len = read_u16(data + p);
    p += 2 + inst_len;

    /* Decode flags */
    uint8_t *flags = (uint8_t *)malloc((size_t)num_points);
    if (!flags) { free(end_pts); return -1; }

    int fi = 0;
    while (fi < num_points) {
        if (p >= len) { free(flags); free(end_pts); return -1; }
        uint8_t f = data[p++];
        flags[fi++] = f;
        if (f & 0x08) { /* repeat flag */
            if (p >= len) { free(flags); free(end_pts); return -1; }
            uint8_t repeat = data[p++];
            for (int r = 0; r < repeat && fi < num_points; r++)
                flags[fi++] = f;
        }
    }

    /* Decode x coordinates (delta-encoded) */
    int16_t *xs = (int16_t *)malloc((size_t)num_points * 2);
    int16_t *ys = (int16_t *)malloc((size_t)num_points * 2);
    if (!xs || !ys) {
        if (xs) free(xs);
        if (ys) free(ys);
        free(flags);
        free(end_pts);
        return -1;
    }

    int16_t val = 0;
    for (int i = 0; i < num_points; i++) {
        uint8_t f = flags[i];
        if (f & 0x02) { /* x is 1 byte */
            if (p >= len) goto fail;
            uint8_t d = data[p++];
            val += (f & 0x10) ? d : -(int16_t)d;
        } else if (!(f & 0x10)) { /* x is 2 bytes */
            if (p + 2 > len) goto fail;
            val += read_i16(data + p);
            p += 2;
        }
        /* else: same as previous (delta = 0) */
        xs[i] = val;
    }

    /* Decode y coordinates */
    val = 0;
    for (int i = 0; i < num_points; i++) {
        uint8_t f = flags[i];
        if (f & 0x04) { /* y is 1 byte */
            if (p >= len) goto fail;
            uint8_t d = data[p++];
            val += (f & 0x20) ? d : -(int16_t)d;
        } else if (!(f & 0x20)) { /* y is 2 bytes */
            if (p + 2 > len) goto fail;
            val += read_i16(data + p);
            p += 2;
        }
        ys[i] = val;
    }

    /* Build path from contours */
    int pt = 0;
    for (int c = 0; c < num_contours; c++) {
        int end = end_pts[c];
        int start = pt;
        int count = end - start + 1;
        if (count < 2) { pt = end + 1; continue; }

        /* Find first on-curve point */
        int first_on = -1;
        for (int i = start; i <= end; i++) {
            if (flags[i] & 0x01) { first_on = i; break; }
        }

        if (first_on < 0) {
            /* All off-curve: create implicit on-curve between first two */
            fix26_6 mx = FIX26_6((xs[start] + xs[start + 1]) / 2);
            fix26_6 my = FIX26_6((ys[start] + ys[start + 1]) / 2);
            gfx_path_move_to(path, mx, my);
            first_on = start;
        } else {
            gfx_path_move_to(path, FIX26_6(xs[first_on]),
                              FIX26_6(ys[first_on]));
        }

        /* Walk through points starting after first_on */
        int i = first_on;
        int prev_off = -1;
        for (int step = 0; step < count; step++) {
            int next = start + ((i - start + 1) % count);
            int on_curve = flags[next] & 0x01;

            if (on_curve) {
                if (prev_off >= 0) {
                    gfx_path_quad_to(path,
                                      FIX26_6(xs[prev_off]),
                                      FIX26_6(ys[prev_off]),
                                      FIX26_6(xs[next]),
                                      FIX26_6(ys[next]));
                    prev_off = -1;
                } else {
                    gfx_path_line_to(path, FIX26_6(xs[next]),
                                      FIX26_6(ys[next]));
                }
            } else {
                if (prev_off >= 0) {
                    /* Implicit on-curve midpoint */
                    fix26_6 mx = FIX26_6((xs[prev_off] + xs[next]) / 2);
                    fix26_6 my = FIX26_6((ys[prev_off] + ys[next]) / 2);
                    gfx_path_quad_to(path,
                                      FIX26_6(xs[prev_off]),
                                      FIX26_6(ys[prev_off]),
                                      mx, my);
                }
                prev_off = next;
            }
            i = next;
        }

        /* Close: handle trailing off-curve point */
        if (prev_off >= 0) {
            gfx_path_quad_to(path,
                              FIX26_6(xs[prev_off]),
                              FIX26_6(ys[prev_off]),
                              FIX26_6(xs[first_on]),
                              FIX26_6(ys[first_on]));
        }
        gfx_path_close(path);
        pt = end + 1;
    }

    free(ys);
    free(xs);
    free(flags);
    free(end_pts);
    return 0;

fail:
    free(ys);
    free(xs);
    free(flags);
    free(end_pts);
    return -1;
}

int ttf_glyph_outline(ttf_font_t *font, uint16_t glyph_id,
                        gfx_path_t *path) {
    if (glyph_id >= font->num_glyphs) return -1;

    uint32_t g_off = glyf_offset(font, glyph_id);
    uint32_t g_next = glyf_offset(font, glyph_id + 1);
    if (g_off == g_next) return 0; /* empty glyph (e.g. space) */

    uint32_t abs_off = font->off_glyf + g_off;
    if (abs_off + 10 > font->data_len) return -1;

    int16_t num_contours = read_i16(font->data + abs_off);

    if (num_contours >= 0) {
        return decode_simple_glyph(font, abs_off, num_contours, path);
    } else {
        /* Compound glyph */
        uint32_t p = abs_off + 10;
        uint16_t comp_flags;
        do {
            if (p + 4 > font->data_len) return -1;
            comp_flags = read_u16(font->data + p);
            uint16_t comp_glyph = read_u16(font->data + p + 2);
            p += 4;

            int16_t dx = 0, dy = 0;
            if (comp_flags & 0x0001) { /* ARG_1_AND_2_ARE_WORDS */
                if (p + 4 > font->data_len) return -1;
                dx = read_i16(font->data + p); p += 2;
                dy = read_i16(font->data + p); p += 2;
            } else {
                if (p + 2 > font->data_len) return -1;
                dx = (int8_t)font->data[p++];
                dy = (int8_t)font->data[p++];
            }

            /* Skip transformation matrix if present */
            if (comp_flags & 0x0008) p += 2;       /* WE_HAVE_A_SCALE */
            else if (comp_flags & 0x0040) p += 4;  /* WE_HAVE_AN_X_AND_Y_SCALE */
            else if (comp_flags & 0x0080) p += 8;  /* WE_HAVE_A_TWO_BY_TWO */

            /* Recursively decode the component glyph */
            gfx_path_t sub;
            gfx_path_init(&sub);
            if (ttf_glyph_outline(font, comp_glyph, &sub) == 0) {
                /* Apply translation (dx, dy) and append to main path */
                fix26_6 fdx = FIX26_6(dx);
                fix26_6 fdy = FIX26_6(dy);
                for (int j = 0; j < sub.count; j++) {
                    gfx_path_cmd_t *cmd = &sub.cmds[j];
                    switch (cmd->cmd) {
                    case PATH_CMD_MOVE:
                        gfx_path_move_to(path, cmd->x + fdx, cmd->y + fdy);
                        break;
                    case PATH_CMD_LINE:
                        gfx_path_line_to(path, cmd->x + fdx, cmd->y + fdy);
                        break;
                    case PATH_CMD_QUAD:
                        gfx_path_quad_to(path, cmd->cx + fdx, cmd->cy + fdy,
                                          cmd->x + fdx, cmd->y + fdy);
                        break;
                    case PATH_CMD_CLOSE:
                        gfx_path_close(path);
                        break;
                    }
                }
            }
            gfx_path_free(&sub);

        } while (comp_flags & 0x0020); /* MORE_COMPONENTS */

        return 0;
    }
}

/* ═══ Glyph rasterization + caching ═════════════════════════ */

static void ttf_flush_cache(ttf_font_t *font) {
    for (int i = 0; i < TTF_CACHE_SIZE; i++) {
        if (font->cache[i].alpha) {
            free(font->cache[i].alpha);
            font->cache[i].alpha = 0;
        }
    }
}

static ttf_glyph_cache_t *ttf_rasterize_glyph(ttf_font_t *font,
                                                 uint16_t codepoint,
                                                 int size_px) {
    if (codepoint >= TTF_CACHE_SIZE) return 0;

    /* Check if cache needs flushing for new size */
    if (font->cache_size_px != size_px) {
        ttf_flush_cache(font);
        font->cache_size_px = size_px;
    }

    ttf_glyph_cache_t *gc = &font->cache[codepoint];
    if (gc->alpha) return gc; /* cache hit */

    /* Get glyph outline */
    uint16_t glyph_id = ttf_char_to_glyph(font, codepoint);
    int advance_units = ttf_glyph_advance(font, glyph_id);

    gfx_path_t outline;
    gfx_path_init(&outline);
    if (ttf_glyph_outline(font, glyph_id, &outline) < 0) {
        gfx_path_free(&outline);
        return 0;
    }

    if (outline.count == 0) {
        /* Empty glyph (space, etc.) */
        gfx_path_free(&outline);
        gc->alpha = 0;
        gc->w = 0;
        gc->h = 0;
        gc->bearing_x = 0;
        gc->bearing_y = 0;
        gc->advance = (int)((int64_t)advance_units * size_px / font->units_per_em);
        return gc;
    }

    /* Scale all coordinates: font_units → pixels */
    fix26_6 scale = FIX26_6_FRAC(size_px, (int)font->units_per_em);
    int asc_px = (int)((int64_t)font->ascender * size_px / font->units_per_em);

    /* Find bounding box and scale+flip */
    fix26_6 xmin = 0x7FFFFFFF, ymin = 0x7FFFFFFF;
    fix26_6 xmax = -0x7FFFFFFF, ymax = -0x7FFFFFFF;

    for (int i = 0; i < outline.count; i++) {
        gfx_path_cmd_t *cmd = &outline.cmds[i];
        if (cmd->cmd == PATH_CMD_CLOSE) continue;

        /* Scale and flip y */
        cmd->x = FIX26_6_MUL(cmd->x, scale);
        cmd->y = FIX26_6(asc_px) - FIX26_6_MUL(cmd->y, scale);
        if (cmd->cmd == PATH_CMD_QUAD) {
            cmd->cx = FIX26_6_MUL(cmd->cx, scale);
            cmd->cy = FIX26_6(asc_px) - FIX26_6_MUL(cmd->cy, scale);
            if (cmd->cx < xmin) xmin = cmd->cx;
            if (cmd->cx > xmax) xmax = cmd->cx;
            if (cmd->cy < ymin) ymin = cmd->cy;
            if (cmd->cy > ymax) ymax = cmd->cy;
        }
        if (cmd->x < xmin) xmin = cmd->x;
        if (cmd->x > xmax) xmax = cmd->x;
        if (cmd->y < ymin) ymin = cmd->y;
        if (cmd->y > ymax) ymax = cmd->y;
    }

    /* Compute bitmap dimensions */
    int bx = FIX26_6_FLOOR(xmin);
    int by = FIX26_6_FLOOR(ymin);
    int bw = FIX26_6_CEIL(xmax) - bx + 1;
    int bh = FIX26_6_CEIL(ymax) - by + 1;
    if (bw <= 0 || bh <= 0 || bw > 256 || bh > 256) {
        gfx_path_free(&outline);
        return 0;
    }

    /* Translate path so bbox starts at (0,0) */
    fix26_6 ox = FIX26_6(bx);
    fix26_6 oy = FIX26_6(by);
    for (int i = 0; i < outline.count; i++) {
        gfx_path_cmd_t *cmd = &outline.cmds[i];
        if (cmd->cmd == PATH_CMD_CLOSE) continue;
        cmd->x -= ox; cmd->y -= oy;
        if (cmd->cmd == PATH_CMD_QUAD) {
            cmd->cx -= ox; cmd->cy -= oy;
        }
    }

    /* Rasterize into alpha bitmap using a temporary surface */
    uint8_t *alpha = (uint8_t *)calloc((size_t)bw * (size_t)bh, 1);
    if (!alpha) { gfx_path_free(&outline); return 0; }

    /* Create a temporary 32-bit surface for rasterization */
    uint32_t *tmp = (uint32_t *)calloc((size_t)bw * (size_t)bh, 4);
    if (!tmp) { free(alpha); gfx_path_free(&outline); return 0; }

    gfx_surface_t surf = { tmp, bw, bh, bw };
    gfx_surf_fill_path_aa(&surf, &outline, 0x00FFFFFF);

    /* Extract coverage: white pixel = fully covered */
    for (int i = 0; i < bw * bh; i++) {
        uint32_t px = tmp[i];
        /* The rasterizer wrote white (0xFFFFFF) where covered.
           Use the red channel as alpha. */
        alpha[i] = (uint8_t)((px >> 16) & 0xFF);
    }

    free(tmp);
    gfx_path_free(&outline);

    gc->alpha = alpha;
    gc->w = bw;
    gc->h = bh;
    gc->bearing_x = bx;
    gc->bearing_y = by;
    gc->advance = (int)((int64_t)advance_units * size_px / font->units_per_em);

    return gc;
}

/* ═══ TTF string rendering ═══════════════════════════════════ */

void gfx_surf_draw_string_ttf(gfx_surface_t *s, int x, int y,
                                const char *str, uint32_t color,
                                ttf_font_t *font, int size_px) {
    uint32_t cr = (color >> 16) & 0xFF;
    uint32_t cg = (color >> 8) & 0xFF;
    uint32_t cb = color & 0xFF;

    int pen_x = x;

    while (*str) {
        uint16_t cp = (uint16_t)(unsigned char)*str;
        str++;

        ttf_glyph_cache_t *gc = ttf_rasterize_glyph(font, cp, size_px);
        if (!gc) continue;

        if (gc->alpha && gc->w > 0 && gc->h > 0) {
            int dx = pen_x + gc->bearing_x;
            int dy = y + gc->bearing_y;

            for (int row = 0; row < gc->h; row++) {
                int sy = dy + row;
                if (sy < 0 || sy >= s->h) continue;
                uint32_t *dst = s->buf + sy * s->pitch;
                uint8_t *src = gc->alpha + row * gc->w;

                for (int col = 0; col < gc->w; col++) {
                    int sx = dx + col;
                    if (sx < 0 || sx >= s->w) continue;
                    uint8_t a = src[col];
                    if (a == 0) continue;
                    if (a == 255) {
                        dst[sx] = color;
                    } else {
                        uint32_t inv = 255 - a;
                        uint32_t dp = dst[sx];
                        uint32_t dr = (dp >> 16) & 0xFF;
                        uint32_t dg = (dp >> 8) & 0xFF;
                        uint32_t db = dp & 0xFF;
                        uint32_t or_ = (cr * a + dr * inv) / 255;
                        uint32_t og = (cg * a + dg * inv) / 255;
                        uint32_t ob = (cb * a + db * inv) / 255;
                        dst[sx] = (or_ << 16) | (og << 8) | ob;
                    }
                }
            }
        }

        pen_x += gc->advance;
    }
}

void gfx_draw_string_ttf(int x, int y, const char *str, uint32_t color,
                           ttf_font_t *font, int size_px) {
    gfx_surface_t s = gfx_get_surface();
    gfx_surf_draw_string_ttf(&s, x, y, str, color, font, size_px);
}
