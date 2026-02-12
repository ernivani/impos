#ifndef _KERNEL_BEEP_H
#define _KERNEL_BEEP_H

#include <stdint.h>

/* Play a tone at given frequency (Hz) for duration (ms) */
void beep(uint32_t freq, uint32_t duration_ms);

/* Common beep presets */
void beep_ok(void);       /* short positive beep */
void beep_error(void);    /* error double-beep */
void beep_notify(void);   /* notification chime */
void beep_startup(void);  /* boot jingle */

#endif
