/* ui_token.h — UIKit design tokens
 *
 * Single source of truth for all visual constants.
 * Replaces the scattered #defines and ui_theme struct fields.
 *
 * Color format: GFX_RGB(r,g,b) = 0x00RRGGBB
 * All colours match the mockup.html palette exactly.
 */

#ifndef _KERNEL_UI_TOKEN_H
#define _KERNEL_UI_TOKEN_H

#include <kernel/gfx.h>

/* ═══ Background surfaces ═══════════════════════════════════════ */

#define TOK_BG_BASE        GFX_RGB(  5,  8, 16)   /* #050810 darkest */
#define TOK_BG_SURFACE     GFX_RGB( 12, 20, 40)   /* #0C1428 windows/cards */
#define TOK_BG_OVERLAY     GFX_RGB( 16, 24, 40)   /* #101828 floating panels */
#define TOK_BG_ELEVATED    GFX_RGB( 20, 28, 44)   /* #141C2C tooltips/menus */
#define TOK_BG_SELECTED    GFX_RGB( 20, 40, 80)   /* list row selected */
#define TOK_BG_HOVER       GFX_RGB(255,255,255)   /* use with opacity 6% */

/* ═══ Window chrome (matches wm2.c / mockup.html) ═══════════════ */

#define TOK_WIN_BODY       GFX_RGB( 18, 24, 36)   /* #121824 */
#define TOK_WIN_TITLEBAR   GFX_RGB( 26, 32, 48)   /* #1A2030 */
#define TOK_WIN_BORDER     GFX_RGB(255,255,255)   /* use with opacity 10% */

/* ═══ Menubar ════════════════════════════════════════════════════ */

#define TOK_MENUBAR_BG     GFX_RGB( 12, 16, 22)   /* rgba(12,16,22,.72) */
#define TOK_MENUBAR_ALPHA  184                     /* 72% opacity */

/* ═══ Accent ═════════════════════════════════════════════════════ */

#define TOK_ACCENT         GFX_RGB( 52,120,246)   /* #3478F6 brand blue */
#define TOK_ACCENT_HOVER   GFX_RGB( 74,138,255)   /* lighter on hover */
#define TOK_ACCENT_PRESS   GFX_RGB( 30, 90,210)   /* darker on press */
#define TOK_ACCENT_DIM     GFX_RGB( 52,120,246)   /* use with opacity 30% */

/* ═══ Text ═══════════════════════════════════════════════════════ */

#define TOK_TEXT_PRIMARY   GFX_RGB(205,214,244)   /* #CDD6F4 */
#define TOK_TEXT_SECONDARY GFX_RGB(166,173,200)   /* #A6ADC8 */
#define TOK_TEXT_DIM       GFX_RGB( 85, 95,128)   /* #55607F */
#define TOK_TEXT_FAINT     GFX_RGB( 40, 50, 80)   /* barely visible */
#define TOK_TEXT_ON_ACCENT GFX_RGB(255,255,255)
#define TOK_TEXT_ERROR     GFX_RGB(243,139,168)   /* #F38BA8 */
#define TOK_TEXT_SUCCESS   GFX_RGB(166,227,161)   /* #A6E3A1 */

/* ═══ Traffic-light buttons (matches mockup.html exactly) ════════ */

#define TOK_BTN_CLOSE      GFX_RGB(255, 95, 87)   /* #FF5F57 */
#define TOK_BTN_MIN        GFX_RGB(255,189, 46)   /* #FFBD2E */
#define TOK_BTN_MAX        GFX_RGB( 40,200, 64)   /* #28C840 */

/* ═══ Interactive states ═════════════════════════════════════════ */

#define TOK_HOVER_OVERLAY  GFX_RGB(255,255,255)   /* use with opacity 7% */
#define TOK_PRESS_OVERLAY  GFX_RGB(  0,  0,  0)   /* use with opacity 15% */
#define TOK_FOCUS_RING     GFX_RGB( 52,120,246)   /* same as accent */

/* ═══ Borders ════════════════════════════════════════════════════ */

#define TOK_BORDER         GFX_RGB(255,255,255)   /* use with opacity 8-10% */
#define TOK_BORDER_FOCUS   GFX_RGB( 52,120,246)

/* ═══ Spacing (pixels) ═══════════════════════════════════════════ */

#define TOK_PAD_XS     4
#define TOK_PAD_SM     8
#define TOK_PAD_MD    12
#define TOK_PAD_LG    20
#define TOK_PAD_XL    28

#define TOK_GAP_XS     2
#define TOK_GAP_SM     4
#define TOK_GAP_MD     8
#define TOK_GAP_LG    12
#define TOK_GAP_XL    16

/* ═══ Corner radii ═══════════════════════════════════════════════ */

#define TOK_RADIUS_XS    4
#define TOK_RADIUS_SM    7
#define TOK_RADIUS_MD   10
#define TOK_RADIUS_LG   14
#define TOK_RADIUS_XL   18
#define TOK_RADIUS_PILL 100   /* pill shape */
#define TOK_RADIUS_WIN   12   /* window chrome (matches wm2.c) */

/* ═══ Shadow levels ══════════════════════════════════════════════ */

#define TOK_SHADOW_NONE   0
#define TOK_SHADOW_SM     1   /* subtle: y+4 blur:12 */
#define TOK_SHADOW_MD     2   /* window: y+8 blur:24 */
#define TOK_SHADOW_LG     3   /* overlay: y+16 blur:48 */

/* ═══ Font sizes ═════════════════════════════════════════════════ */

#define TOK_FONT_XS    1   /* scale factor for gfx_surf_draw_string_smooth */
#define TOK_FONT_SM    1
#define TOK_FONT_MD    2
#define TOK_FONT_LG    2
#define TOK_FONT_XL    3

/* ═══ Z-order layers (compositor) ════════════════════════════════ */

#define TOK_LAYER_WALLPAPER  0
#define TOK_LAYER_WINDOWS    1
#define TOK_LAYER_OVERLAY    2
#define TOK_LAYER_CURSOR     3

/* ═══ Menubar / shell geometry ════════════════════════════════════ */

#define TOK_MENUBAR_H   28
#define TOK_DOCK_H      56
#define TOK_TITLEBAR_H  38

#endif /* _KERNEL_UI_TOKEN_H */
