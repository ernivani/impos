#ifndef _KERNEL_WALLPAPER_H
#define _KERNEL_WALLPAPER_H

#include <stdint.h>

/* Wallpaper style indices */
#define WALLPAPER_MOUNTAINS  0
#define WALLPAPER_GRADIENT   1
#define WALLPAPER_GEOMETRIC  2
#define WALLPAPER_STARS      3
#define WALLPAPER_WAVES      4
#define WALLPAPER_STYLE_COUNT 5

/* Max themes per style */
#define WALLPAPER_MAX_THEMES 4

/* Initialize engine; must be called before any draw. */
void wallpaper_init(void);

/* Draw the current wallpaper into buf (ARGB, w*h pixels). t = PIT ticks. */
void wallpaper_draw(uint32_t *buf, int w, int h, uint32_t t);

/* Draw a static thumbnail for style/theme into buf (any resolution). */
void wallpaper_draw_thumbnail(uint32_t *buf, int w, int h,
                              int style_idx, int theme_idx);

/* Switch to a new style (and optionally reset theme). */
void wallpaper_set_style(int style_idx, int theme_idx);

/* Switch theme within the current style (triggers cross-fade). */
void wallpaper_set_theme(int theme_idx);

/* Query current selection. */
int wallpaper_get_style(void);
int wallpaper_get_theme(void);

/* Number of themes for a given style (1-4). */
int wallpaper_theme_count(int style_idx);

/* Name strings */
const char *wallpaper_style_name(int style_idx);
const char *wallpaper_theme_name(int style_idx, int theme_idx);

/* Representative color dot for a theme (for Settings UI). */
uint32_t wallpaper_theme_color(int style_idx, int theme_idx);

#endif
