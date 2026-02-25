/* anim.c — Animation engine: integer tweens with easing.
 *
 * All tweens operate on int values to avoid floats in hot paths.
 * Call anim_tick(dt_ms) once per frame from the desktop loop.
 */
#include <kernel/anim.h>
#include <string.h>

typedef struct {
    int      *target;
    int       from, to;
    uint32_t  elapsed_ms;
    uint32_t  duration_ms;
    int       easing;
    int       active;
} tween_t;

static tween_t tweens[ANIM_MAX_TWEENS];

void anim_init(void) {
    memset(tweens, 0, sizeof(tweens));
}

/* Easing functions — input t in [0, 1024], output in [0, 1024] */
static int ease_apply(int t, int easing) {
    switch (easing) {
    case ANIM_EASE_IN:
        /* t² */
        return t * t / 1024;
    case ANIM_EASE_OUT:
        /* 1-(1-t)² */
        { int u = 1024 - t; return 1024 - u * u / 1024; }
    case ANIM_SPRING:
        /* overshoot: ease-out with slight bounce at end */
        { int u = 1024 - t;
          int base = 1024 - u * u / 1024;
          /* Add overshoot: +10% at 70% progress then damp */
          if (t < 768) {
              int extra = (t * 102 / 768);
              return base + extra * (1024 - t) / 512;
          }
          return base; }
    default: /* ANIM_LINEAR */
        return t;
    }
}

void anim_tick(uint32_t dt_ms) {
    int i;
    for (i = 0; i < ANIM_MAX_TWEENS; i++) {
        tween_t *tw = &tweens[i];
        if (!tw->active) continue;

        tw->elapsed_ms += dt_ms;
        if (tw->elapsed_ms >= tw->duration_ms) {
            *tw->target = tw->to;
            tw->active = 0;
        } else {
            int t = (int)(tw->elapsed_ms * 1024 / tw->duration_ms);
            int et = ease_apply(t, tw->easing);
            *tw->target = tw->from + (tw->to - tw->from) * et / 1024;
        }
    }
}

int anim_start(int *value, int from, int to,
               uint32_t duration_ms, int easing) {
    int i;
    /* Cancel any existing tween for this target */
    for (i = 0; i < ANIM_MAX_TWEENS; i++)
        if (tweens[i].active && tweens[i].target == value)
            tweens[i].active = 0;

    for (i = 0; i < ANIM_MAX_TWEENS; i++) {
        if (!tweens[i].active) {
            tweens[i].target      = value;
            tweens[i].from        = from;
            tweens[i].to          = to;
            tweens[i].elapsed_ms  = 0;
            tweens[i].duration_ms = duration_ms ? duration_ms : 1;
            tweens[i].easing      = easing;
            tweens[i].active      = 1;
            *value = from;
            return i;
        }
    }
    return -1;
}

void anim_cancel(int id) {
    if (id >= 0 && id < ANIM_MAX_TWEENS)
        tweens[id].active = 0;
}

int anim_any_active(void) {
    int i;
    for (i = 0; i < ANIM_MAX_TWEENS; i++)
        if (tweens[i].active) return 1;
    return 0;
}

int anim_active(int id) {
    if (id < 0 || id >= ANIM_MAX_TWEENS) return 0;
    return tweens[id].active;
}
