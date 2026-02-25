#ifndef _KERNEL_ICON_CACHE_H
#define _KERNEL_ICON_CACHE_H

#include <stdint.h>

/* Icon identifiers */
#define ICON_TERMINAL   0
#define ICON_FILES      1
#define ICON_BROWSER    2
#define ICON_MUSIC      3
#define ICON_SETTINGS   4
#define ICON_MONITOR    5
#define ICON_EMAIL      6
#define ICON_CHAT       7
#define ICON_VIDEO      8
#define ICON_CODE       9
#define ICON_IMAGE      10
#define ICON_PDF        11
#define ICON_GAMEPAD    12
#define ICON_DISK       13
#define ICON_USERS      14
#define ICON_DOWNLOAD   15
#define ICON_TABLE      16
#define ICON_PEN        17
#define ICON_CALENDAR   18
#define ICON_RADIO      19
#define ICON_COUNT      20

/* Initialize the icon cache. */
void icon_cache_init(void);

/* Draw a 2-letter avatar icon (rounded rect + letters).
   dst: target pixel buffer (ARGB)
   pitch: row stride in pixels
   x, y: top-left position in dst
   size: icon width/height in pixels
   bg: background fill color (ARGB)
   letter: 1-2 character abbreviation */
void icon_draw_letter(uint32_t *dst, int pitch, int x, int y,
                      int size, uint32_t bg, const char *letters);

/* Draw a symbolic icon by ID at (x,y) size×size into dst.
   Falls back to letter avatar if icon_id is unknown.
   fg: foreground/symbol color
   bg: background fill */
void icon_draw(int icon_id, uint32_t *dst, int pitch,
               int x, int y, int size, uint32_t bg, uint32_t fg);

#endif
