#ifndef _PTHREAD_H
#define _PTHREAD_H

#include <stdint.h>

/* ── Types ──────────────────────────────────────────────────────── */

typedef int pthread_t;              /* task slot index */
typedef int pthread_attr_t;         /* unused for now */

typedef struct {
    volatile int lock;              /* 0=unlocked, 1=locked */
} pthread_mutex_t;

typedef struct {
    volatile int seq;               /* sequence counter for wakeups */
} pthread_cond_t;

typedef int pthread_mutexattr_t;    /* unused */
typedef int pthread_condattr_t;     /* unused */

/* ── Initializers ───────────────────────────────────────────────── */

#define PTHREAD_MUTEX_INITIALIZER { 0 }
#define PTHREAD_COND_INITIALIZER  { 0 }

/* ── Thread API ─────────────────────────────────────────────────── */

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg);
int pthread_join(pthread_t thread, void **retval);
void pthread_exit(void *retval);

/* ── Mutex API ──────────────────────────────────────────────────── */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr);
int pthread_mutex_destroy(pthread_mutex_t *mutex);
int pthread_mutex_lock(pthread_mutex_t *mutex);
int pthread_mutex_trylock(pthread_mutex_t *mutex);
int pthread_mutex_unlock(pthread_mutex_t *mutex);

/* ── Condition Variable API ─────────────────────────────────────── */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr);
int pthread_cond_destroy(pthread_cond_t *cond);
int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex);
int pthread_cond_signal(pthread_cond_t *cond);
int pthread_cond_broadcast(pthread_cond_t *cond);

#endif
