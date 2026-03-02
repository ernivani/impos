/*
 * pthread.c — POSIX threads implementation for ImposOS
 *
 * This is a kernel-mode implementation that wraps ImposOS primitives.
 * Mutexes and condvars use the kernel futex for blocking, with
 * atomic compare-and-swap for the uncontended fast path.
 *
 * For ELF Linux binaries, the libc linked into those binaries (e.g., musl)
 * provides its own pthreads that call clone()/futex() via INT 0x80.
 * This implementation is for kernel-space use only.
 */

#ifdef __is_libk

#include <pthread.h>
#include <kernel/task.h>
#include <stdint.h>

/* sys_futex is in kernel/arch/i386/sys/futex.c */
extern int sys_futex(uint32_t *uaddr, int op, uint32_t val);
#define FUTEX_WAIT  0
#define FUTEX_WAKE  1

/* ── Thread creation ────────────────────────────────────────────── */

/* Wrapper struct to pass start_routine + arg to the thread entry */
typedef struct {
    void *(*start_routine)(void *);
    void *arg;
} thread_start_t;

/* Only one thread can be starting at a time (kernel is single-address-space) */
static volatile thread_start_t pending_start;

static void thread_trampoline(void) {
    void *(*fn)(void *) = pending_start.start_routine;
    void *arg = pending_start.arg;
    pending_start.start_routine = 0;  /* signal that we've copied the args */

    fn(arg);  /* call the user function */

    task_exit();  /* never returns */
}

int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
    (void)attr;

    pending_start.start_routine = start_routine;
    pending_start.arg = arg;

    int tid = task_create_thread("pthread", thread_trampoline, 1);
    if (tid < 0)
        return -1;  /* EAGAIN */

    if (thread)
        *thread = tid;

    return 0;
}

int pthread_join(pthread_t thread, void **retval) {
    (void)retval;  /* we don't support return values yet */

    /* Busy-wait for thread to complete (simple implementation).
     * A proper implementation would use waitpid or TASK_STATE_ZOMBIE check. */
    int pid = task_get_pid(thread);
    if (pid < 0) return 0;  /* thread already gone */

    int wstatus = 0;
    sys_waitpid(pid, &wstatus, 0);
    return 0;
}

void pthread_exit(void *retval) {
    (void)retval;
    task_exit();
    __builtin_unreachable();
}

/* ── Mutex ──────────────────────────────────────────────────────── */

int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr) {
    (void)attr;
    mutex->lock = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
    (void)mutex;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
    while (1) {
        /* Try to acquire: CAS from 0 → 1 */
        int old = __sync_val_compare_and_swap(&mutex->lock, 0, 1);
        if (old == 0)
            return 0;  /* acquired! */

        /* Contended — sleep until woken */
        sys_futex((uint32_t *)&mutex->lock, FUTEX_WAIT, 1);
    }
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
    int old = __sync_val_compare_and_swap(&mutex->lock, 0, 1);
    return (old == 0) ? 0 : -1;  /* 0 = success, -1 = EBUSY */
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
    __sync_lock_release(&mutex->lock);  /* store 0 with release semantics */
    sys_futex((uint32_t *)&mutex->lock, FUTEX_WAKE, 1);
    return 0;
}

/* ── Condition Variable ─────────────────────────────────────────── */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
    (void)attr;
    cond->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
    (void)cond;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
    int seq = cond->seq;

    /* Release the mutex */
    pthread_mutex_unlock(mutex);

    /* Wait for the sequence counter to change */
    sys_futex((uint32_t *)&cond->seq, FUTEX_WAIT, seq);

    /* Re-acquire the mutex */
    pthread_mutex_lock(mutex);

    return 0;
}

int pthread_cond_signal(pthread_cond_t *cond) {
    __sync_fetch_and_add(&cond->seq, 1);
    sys_futex((uint32_t *)&cond->seq, FUTEX_WAKE, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
    __sync_fetch_and_add(&cond->seq, 1);
    sys_futex((uint32_t *)&cond->seq, FUTEX_WAKE, INT32_MAX);
    return 0;
}

#endif /* __is_libk */
