#ifndef _KERNEL_ANIM_H
#define _KERNEL_ANIM_H

#include <stdint.h>

/* Easing types */
#define ANIM_LINEAR   0
#define ANIM_EASE_IN  1
#define ANIM_EASE_OUT 2
#define ANIM_SPRING   3   /* overshoot spring */

/* Max simultaneous tweens */
#define ANIM_MAX_TWEENS 32

/* Initialize animation engine. */
void anim_init(void);

/* Advance all tweens by dt_ms milliseconds. Call once per frame. */
void anim_tick(uint32_t dt_ms);

/* Start an integer tween.
   *value is modified each tick until it reaches 'to'.
   Returns tween id (>= 0) or -1 on failure. */
int anim_start(int *value, int from, int to,
               uint32_t duration_ms, int easing);

/* Cancel a running tween by id. */
void anim_cancel(int id);

/* Returns 1 if any tween is still running. */
int anim_any_active(void);

/* Returns 1 if specific tween is still running. */
int anim_active(int id);

#endif
