#ifndef _KERNEL_GFX_TTF_H
#define _KERNEL_GFX_TTF_H

#include <stdint.h>
#include <kernel/gfx.h>
#include <kernel/gfx_path.h>

/* ═══ Glyph cache entry ══════════════════════════════════════ */

typedef struct {
    uint8_t *alpha;     /* rasterized alpha bitmap (NULL if not cached) */
    int w, h;           /* bitmap dimensions */
    int bearing_x;      /* left-side bearing in pixels */
    int bearing_y;      /* top bearing in pixels (from baseline) */
    int advance;        /* horizontal advance in pixels */
} ttf_glyph_cache_t;

/* ═══ TTF font handle ════════════════════════════════════════ */

#define TTF_CACHE_SIZE 256

typedef struct {
    const uint8_t *data;
    uint32_t data_len;

    /* Table offsets */
    uint32_t off_head, off_maxp, off_cmap, off_loca;
    uint32_t off_glyf, off_hhea, off_hmtx;

    /* Parsed metrics */
    uint16_t units_per_em;
    uint16_t num_glyphs;
    uint16_t num_h_metrics;
    int16_t  ascender, descender, line_gap;
    int16_t  index_to_loc_fmt;

    /* cmap subtable offsets */
    uint32_t cmap_fmt4_off;
    uint32_t cmap_fmt0_off;

    /* Per-size glyph cache */
    ttf_glyph_cache_t cache[TTF_CACHE_SIZE];
    int cache_size_px;
} ttf_font_t;

/* ═══ TTF loading ════════════════════════════════════════════ */

int  ttf_load(ttf_font_t *font, const uint8_t *data, uint32_t len);
void ttf_free(ttf_font_t *font);

/* ═══ TTF queries ════════════════════════════════════════════ */

uint16_t ttf_char_to_glyph(ttf_font_t *font, uint16_t codepoint);
int      ttf_glyph_outline(ttf_font_t *font, uint16_t glyph_id,
                            gfx_path_t *path);
int      ttf_glyph_advance(ttf_font_t *font, uint16_t glyph_id);

/* ═══ TTF string rendering ═══════════════════════════════════ */

void gfx_surf_draw_string_ttf(gfx_surface_t *s, int x, int y,
                                const char *str, uint32_t color,
                                ttf_font_t *font, int size_px);
void gfx_draw_string_ttf(int x, int y, const char *str, uint32_t color,
                           ttf_font_t *font, int size_px);

/* ═══ Built-in vector font (auto-traced from font8x16) ══════ */

void gfx_builtin_font_init(void);

void gfx_surf_draw_char_vec(gfx_surface_t *s, int x, int y,
                              char c, uint32_t color, int size_px);
void gfx_surf_draw_string_vec(gfx_surface_t *s, int x, int y,
                                const char *str, uint32_t color, int size_px);

void gfx_draw_char_vec(int x, int y, char c, uint32_t color, int size_px);
void gfx_draw_string_vec(int x, int y, const char *str,
                           uint32_t color, int size_px);
int  gfx_string_vec_width(const char *str, int size_px);

#endif
