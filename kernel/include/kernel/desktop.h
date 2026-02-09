#ifndef _KERNEL_DESKTOP_H
#define _KERNEL_DESKTOP_H

#include <stdint.h>
#include <kernel/gfx.h>

/* impOS Mono — pure black, ultra-minimal */
#define DT_BG            0x000000
#define DT_SURFACE       GFX_RGB(20, 20, 20)
#define DT_BORDER        GFX_RGB(30, 30, 30)
#define DT_TEXT           GFX_RGB(230, 230, 230)
#define DT_TEXT_DIM       GFX_RGB(51, 51, 51)
#define DT_TEXT_MED       GFX_RGB(178, 178, 178)
#define DT_TEXT_SUB       GFX_RGB(100, 100, 100)
#define DT_WIN_BG         0x000000
#define DT_TASKBAR_BG    GFX_RGB(15, 15, 15)
#define DT_DOCK_PILL     GFX_RGB(8, 8, 8)
#define DT_ICON          GFX_RGB(190, 190, 190)
#define DT_ICON_HI       GFX_RGB(255, 255, 255)
#define DT_ICON_DIM      GFX_RGB(90, 90, 90)
#define DT_DOT           GFX_RGB(128, 128, 128)
#define DT_SEL_BG        GFX_RGB(30, 30, 30)
#define DT_FIELD_BG      GFX_RGB(20, 20, 20)
#define DT_FIELD_BORDER  GFX_RGB(30, 30, 30)
#define DT_FIELD_FOCUS   GFX_RGB(76, 76, 76)
#define DT_FIELD_PH      GFX_RGB(50, 50, 50)
#define DT_ERROR         GFX_RGB(180, 60, 60)

/* Layout */
#define TASKBAR_H 48

/* Compat aliases */
#define DOCK_H    TASKBAR_H
#define TITLE_H   0
#define WIN_PAD_X 0
#define WIN_PAD_Y 0
#define DOCK_GAP  0

/* Desktop actions returned by desktop_run() */
#define DESKTOP_ACTION_NONE     0
#define DESKTOP_ACTION_FILES    1
#define DESKTOP_ACTION_TERMINAL 2
#define DESKTOP_ACTION_BROWSER  3
#define DESKTOP_ACTION_EDITOR   4
#define DESKTOP_ACTION_SETTINGS 5
#define DESKTOP_ACTION_MONITOR  6
#define DESKTOP_ACTION_POWER    7

void desktop_init(void);
void desktop_draw_dock(void);
void desktop_draw_chrome(void);
void desktop_splash(void);
void desktop_setup(void);
int  desktop_login(void);
int  desktop_run(void);
void desktop_open_terminal(void);
void desktop_close_terminal(void);

#endif
