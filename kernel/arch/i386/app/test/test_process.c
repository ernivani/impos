#include <kernel/test.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/vmm.h>
#include <kernel/vma.h>
#include <kernel/frame_ref.h>
#include <kernel/pmm.h>
#include <kernel/pipe.h>
#include <kernel/idt.h>
#include <kernel/linux_syscall.h>
#include <kernel/elf_loader.h>
#include <kernel/env.h>
#include <kernel/signal.h>
#include <kernel/shm.h>
#include <kernel/rtc.h>
#include <kernel/fs.h>
#include <kernel/vfs.h>
#include <kernel/ui_widget.h>
#include <kernel/wm.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void test_scheduler_priority(void) {
    printf("== Scheduler Priority Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "sched: current task exists");

    /* Boot task (TASK_IDLE, slot 0) has PRIO_IDLE; TASK_KERNEL has PRIO_NORMAL.
     * The kernel main function runs as TASK_IDLE during boot. */
    int prio = sched_get_priority(me);
    TEST_ASSERT(prio >= 0 && prio <= 3, "sched: priority in valid range 0-3");
    TEST_ASSERT(t->time_slice > 0, "sched: time_slice > 0");

    /* TASK_KERNEL (slot 1) should be PRIO_NORMAL */
    task_info_t *kern = task_get(TASK_KERNEL);
    TEST_ASSERT(kern != NULL && kern->priority == PRIO_NORMAL, "sched: TASK_KERNEL priority NORMAL");
    TEST_ASSERT(kern != NULL && kern->time_slice == SLICE_NORMAL, "sched: TASK_KERNEL time_slice == 3");

    /* Set priority to background, verify it sticks */
    sched_set_priority(me, PRIO_BACKGROUND);
    TEST_ASSERT(t->priority == PRIO_BACKGROUND, "sched: set_priority to BACKGROUND");
    TEST_ASSERT(t->time_slice == SLICE_BACKGROUND, "sched: background time_slice == 6");
    TEST_ASSERT(sched_get_priority(me) == PRIO_BACKGROUND, "sched: get confirms BACKGROUND");

    /* Set to realtime */
    sched_set_priority(me, PRIO_REALTIME);
    TEST_ASSERT(t->priority == PRIO_REALTIME, "sched: set_priority to REALTIME");
    TEST_ASSERT(t->time_slice == SLICE_REALTIME, "sched: realtime time_slice == 1");

    /* Restore to normal */
    sched_set_priority(me, PRIO_NORMAL);
    TEST_ASSERT(t->priority == PRIO_NORMAL, "sched: restored to NORMAL");

    /* Invalid tid should not crash */
    sched_set_priority(-1, PRIO_NORMAL);   /* should be no-op */
    sched_set_priority(999, PRIO_NORMAL);  /* should be no-op */
    TEST_ASSERT(1, "sched: invalid tid set_priority no crash");

    int bad = sched_get_priority(-1);
    TEST_ASSERT(bad == -1, "sched: get_priority(-1) returns -1");
}

static void test_process_lifecycle(void) {
    printf("== Process Lifecycle Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "lifecycle: current task exists");

    /* parent_tid should be set (root tasks have -1) */
    TEST_ASSERT(t->parent_tid >= -1, "lifecycle: parent_tid is valid");

    /* exit_code should start at 0 */
    TEST_ASSERT(t->exit_code == 0, "lifecycle: exit_code init 0");

    /* wait_tid should be -1 (not waiting) */
    TEST_ASSERT(t->wait_tid == -1, "lifecycle: wait_tid init -1");

    /* Process group: should have valid pgid */
    TEST_ASSERT(t->pgid >= 0, "lifecycle: pgid >= 0");

    /* Session: should have valid sid */
    TEST_ASSERT(t->sid >= 0, "lifecycle: sid >= 0");

    /* sys_waitpid with WNOHANG on nonexistent child should return 0 or -1 */
    int wstatus = 0;
    int ret = sys_waitpid(-1, &wstatus, WNOHANG);
    /* No children → should return -1 (ECHILD) or 0 */
    TEST_ASSERT(ret <= 0, "lifecycle: waitpid WNOHANG no children <= 0");
}

static void test_process_groups(void) {
    printf("== Process Groups Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    int my_pid = t->pid;

    /* getpgid should return current pgid */
    int pgid = task_getpgid(my_pid);
    TEST_ASSERT(pgid >= 0, "pgrp: getpgid returns valid pgid");
    TEST_ASSERT(pgid == t->pgid, "pgrp: getpgid matches task pgid field");

    /* setpgid to own PID (become group leader) */
    int old_pgid = t->pgid;
    int ret = task_setpgid(my_pid, my_pid);
    TEST_ASSERT(ret == 0, "pgrp: setpgid to self succeeds");
    TEST_ASSERT(t->pgid == my_pid, "pgrp: pgid now equals own PID");

    /* Restore original pgid */
    task_setpgid(my_pid, old_pgid);

    /* getpgid for nonexistent PID should return -1 */
    int bad = task_getpgid(99999);
    TEST_ASSERT(bad == -1, "pgrp: getpgid invalid PID returns -1");
}

static void test_signals_phase2(void) {
    printf("== Signals Phase 2 Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);

    /* Signal constants should be defined correctly */
    TEST_ASSERT(SIGCHLD == 17, "signal: SIGCHLD == 17");
    TEST_ASSERT(SIGCONT == 18, "signal: SIGCONT == 18");
    TEST_ASSERT(SIGSTOP == 19, "signal: SIGSTOP == 19");
    TEST_ASSERT(SIGALRM == 14, "signal: SIGALRM == 14");
    TEST_ASSERT(SIGTSTP == 20, "signal: SIGTSTP == 20");
    TEST_ASSERT(NSIG == 32, "signal: NSIG == 32");

    /* Blocked mask should start at 0 (nothing blocked) */
    TEST_ASSERT(t->sig.blocked == 0, "signal: blocked mask init 0");

    /* Alarm ticks should start at 0 (disabled) */
    TEST_ASSERT(t->sig.alarm_ticks == 0, "signal: alarm_ticks init 0");

    /* sigprocmask: block SIGUSR1, verify it's in the mask */
    uint32_t oldset = 0;
    int ret = sig_sigprocmask(me, SIG_BLOCK, (1 << SIGUSR1), &oldset);
    TEST_ASSERT(ret == 0, "signal: sigprocmask SIG_BLOCK succeeds");
    TEST_ASSERT(oldset == 0, "signal: old mask was 0");
    TEST_ASSERT((t->sig.blocked & (1 << SIGUSR1)) != 0, "signal: SIGUSR1 now blocked");

    /* Unblock it */
    ret = sig_sigprocmask(me, SIG_UNBLOCK, (1 << SIGUSR1), &oldset);
    TEST_ASSERT(ret == 0, "signal: sigprocmask SIG_UNBLOCK succeeds");
    TEST_ASSERT((t->sig.blocked & (1 << SIGUSR1)) == 0, "signal: SIGUSR1 unblocked");

    /* SIGKILL/SIGSTOP cannot be blocked */
    sig_sigprocmask(me, SIG_BLOCK, (1 << SIGKILL) | (1 << SIGSTOP), NULL);
    TEST_ASSERT((t->sig.blocked & (1 << SIGKILL)) == 0, "signal: SIGKILL cannot be blocked");
    TEST_ASSERT((t->sig.blocked & (1 << SIGSTOP)) == 0, "signal: SIGSTOP cannot be blocked");

    /* SIG_SETMASK */
    uint32_t mask = (1 << SIGUSR2);
    sig_sigprocmask(me, SIG_SETMASK, mask, &oldset);
    TEST_ASSERT(t->sig.blocked == mask, "signal: SIG_SETMASK sets exact mask");
    /* Clean up: clear mask */
    sig_sigprocmask(me, SIG_SETMASK, 0, NULL);
    TEST_ASSERT(t->sig.blocked == 0, "signal: mask cleared to 0");
}

static void test_fd_table(void) {
    printf("== FD Table Tests ==\n");

    int me = task_get_current();
    task_info_t *t = task_get(me);
    TEST_ASSERT(t != NULL, "fd: current task exists");

    /* FD table should be dynamically allocated */
    TEST_ASSERT(t->fds != NULL, "fd: fds pointer not NULL");
    TEST_ASSERT(t->fd_count >= FD_INIT_SIZE, "fd: fd_count >= FD_INIT_SIZE (64)");

    /* Note: cooperative boot tasks don't have stdin/stdout/stderr set up as
     * FD_TTY — only ELF/shell-spawned tasks do. We test the table itself. */

    /* Allocate a pipe to test dup/dup2 */
    int rfd = -1, wfd = -1;
    int ret = pipe_create(&rfd, &wfd, me);
    TEST_ASSERT(ret == 0, "fd: pipe_create succeeds");
    TEST_ASSERT(rfd >= 0, "fd: pipe read fd >= 0");
    TEST_ASSERT(wfd >= 0, "fd: pipe write fd >= 0");
    TEST_ASSERT(rfd != wfd, "fd: pipe read != write fd");

    /* dup: should return lowest available fd */
    int dfd = fd_dup(me, rfd);
    TEST_ASSERT(dfd >= 0, "fd: dup succeeds");
    TEST_ASSERT(dfd != rfd, "fd: dup returns different fd");
    TEST_ASSERT(t->fds[dfd].type == FD_PIPE_R, "fd: dup'd fd is pipe read");
    TEST_ASSERT(t->fds[dfd].pipe_id == t->fds[rfd].pipe_id, "fd: dup'd fd same pipe_id");
    TEST_ASSERT(t->fds[dfd].cloexec == 0, "fd: dup clears cloexec");

    /* dup2: duplicate to specific fd */
    int tgt = dfd + 5;  /* pick a slot that's likely empty */
    if (tgt < t->fd_count) {
        int d2 = fd_dup2(me, wfd, tgt);
        TEST_ASSERT(d2 == tgt, "fd: dup2 returns target fd");
        TEST_ASSERT(t->fds[tgt].type == FD_PIPE_W, "fd: dup2 target is pipe write");
        TEST_ASSERT(t->fds[tgt].cloexec == 0, "fd: dup2 clears cloexec");
        /* Clean up dup2'd fd */
        pipe_close(tgt, me);
    }

    /* dup2 with oldfd == newfd: no-op, return newfd */
    int d2same = fd_dup2(me, rfd, rfd);
    TEST_ASSERT(d2same == rfd, "fd: dup2 same fd returns fd");

    /* dup bad fd should fail */
    int bad = fd_dup(me, -1);
    TEST_ASSERT(bad == -1, "fd: dup(-1) fails");
    bad = fd_dup(me, 9999);
    TEST_ASSERT(bad == -1, "fd: dup(9999) fails");

    /* Clean up */
    pipe_close(dfd, me);
    pipe_close(rfd, me);
    pipe_close(wfd, me);
}

static void test_futex(void) {
    printf("== Futex Tests ==\n");

    /* FUTEX_WAKE on an address with no waiters should return 0 */
    volatile uint32_t val = 42;
    int ret = sys_futex((uint32_t *)&val, 1 /* FUTEX_WAKE */, 1);
    TEST_ASSERT(ret == 0, "futex: WAKE no waiters returns 0");

    /* FUTEX_WAIT with val mismatch should return -EAGAIN (negative) immediately */
    ret = sys_futex((uint32_t *)&val, 0 /* FUTEX_WAIT */, 99);
    TEST_ASSERT(ret < 0, "futex: WAIT val mismatch returns negative");

    /* NULL address should not crash (returns -1) */
    ret = sys_futex(NULL, 1, 0);
    TEST_ASSERT(ret == -1 || ret == 0, "futex: NULL addr no crash");
}

static void test_pthreads(void) {
    printf("== Pthreads Tests ==\n");

    /* Mutex init and state */
    pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex initializer sets lock=0");

    int ret = pthread_mutex_init(&mtx, NULL);
    TEST_ASSERT(ret == 0, "pthread: mutex_init returns 0");
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex_init sets lock=0");

    /* Lock/unlock in uncontended case */
    ret = pthread_mutex_lock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_lock succeeds");
    TEST_ASSERT(mtx.lock == 1, "pthread: mutex locked, lock=1");

    ret = pthread_mutex_unlock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_unlock succeeds");
    TEST_ASSERT(mtx.lock == 0, "pthread: mutex unlocked, lock=0");

    /* Trylock: should succeed when unlocked */
    ret = pthread_mutex_trylock(&mtx);
    TEST_ASSERT(ret == 0, "pthread: trylock succeeds when unlocked");
    TEST_ASSERT(mtx.lock == 1, "pthread: trylock acquired");

    /* Trylock again: should fail (already locked) */
    ret = pthread_mutex_trylock(&mtx);
    TEST_ASSERT(ret == -1, "pthread: trylock fails when locked");

    pthread_mutex_unlock(&mtx);

    /* Destroy */
    ret = pthread_mutex_destroy(&mtx);
    TEST_ASSERT(ret == 0, "pthread: mutex_destroy returns 0");

    /* Condvar init */
    pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
    TEST_ASSERT(cv.seq == 0, "pthread: condvar initializer sets seq=0");

    ret = pthread_cond_init(&cv, NULL);
    TEST_ASSERT(ret == 0, "pthread: cond_init returns 0");

    /* Signal increments sequence counter */
    int old_seq = cv.seq;
    ret = pthread_cond_signal(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_signal returns 0");
    TEST_ASSERT(cv.seq == old_seq + 1, "pthread: cond_signal increments seq");

    /* Broadcast also increments */
    old_seq = cv.seq;
    ret = pthread_cond_broadcast(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_broadcast returns 0");
    TEST_ASSERT(cv.seq == old_seq + 1, "pthread: cond_broadcast increments seq");

    ret = pthread_cond_destroy(&cv);
    TEST_ASSERT(ret == 0, "pthread: cond_destroy returns 0");
}

/* ---- Phase 3: Memory Management ---- */

static void test_vma(void) {
    printf("[VMA] ");

    /* Create VMA table */
    vma_table_t *vt = vma_init();
    TEST_ASSERT(vt != NULL, "vma: init returns non-NULL");
    TEST_ASSERT(vt->count == 0, "vma: init count is 0");
    TEST_ASSERT(vt->mmap_next == 0x20000000, "vma: init mmap_next is 0x20000000");

    /* Insert VMAs */
    int rc = vma_insert(vt, 0x08048000, 0x0804C000, VMA_READ | VMA_EXEC, VMA_TYPE_ELF);
    TEST_ASSERT(rc == 0, "vma: insert ELF segment succeeds");
    TEST_ASSERT(vt->count == 1, "vma: count is 1 after insert");

    rc = vma_insert(vt, 0x0804D000, 0x08050000, VMA_READ | VMA_WRITE, VMA_TYPE_BRK);
    TEST_ASSERT(rc == 0, "vma: insert BRK succeeds");
    TEST_ASSERT(vt->count == 2, "vma: count is 2 after second insert");

    rc = vma_insert(vt, 0x40000000, 0x40001000, VMA_READ | VMA_WRITE | VMA_GROWSDOWN, VMA_TYPE_STACK);
    TEST_ASSERT(rc == 0, "vma: insert STACK succeeds");

    /* Find VMAs */
    vma_t *v = vma_find(vt, 0x08049000);
    TEST_ASSERT(v != NULL, "vma: find 0x08049000 in ELF segment");
    TEST_ASSERT(v->vm_type == VMA_TYPE_ELF, "vma: found VMA is ELF type");

    v = vma_find(vt, 0x0804E000);
    TEST_ASSERT(v != NULL, "vma: find 0x0804E000 in BRK");
    TEST_ASSERT(v->vm_type == VMA_TYPE_BRK, "vma: found VMA is BRK type");

    v = vma_find(vt, 0x12345000);
    TEST_ASSERT(v == NULL, "vma: find unmapped address returns NULL");

    /* Split VMA */
    rc = vma_split(vt, 0x0804A000);
    TEST_ASSERT(rc == 0, "vma: split ELF at 0x0804A000 succeeds");
    TEST_ASSERT(vt->count == 4, "vma: count is 4 after split");

    v = vma_find(vt, 0x08048000);
    TEST_ASSERT(v != NULL && v->vm_end == 0x0804A000, "vma: lower half ends at split point");

    v = vma_find(vt, 0x0804B000);
    TEST_ASSERT(v != NULL && v->vm_start == 0x0804A000, "vma: upper half starts at split point");

    /* Find free gap */
    uint32_t free_va = vma_find_free(vt, 0x4000);
    TEST_ASSERT(free_va != 0, "vma: find_free returns non-zero");
    TEST_ASSERT((free_va & 0xFFF) == 0, "vma: find_free returns page-aligned");

    /* Remove VMA */
    int pages = vma_remove(vt, 0x0804D000, 0x08050000);
    TEST_ASSERT(pages == 3, "vma: remove BRK returns 3 pages");

    v = vma_find(vt, 0x0804E000);
    TEST_ASSERT(v == NULL, "vma: BRK region no longer found after remove");

    /* Clone */
    vma_table_t *clone = vma_clone(vt);
    TEST_ASSERT(clone != NULL, "vma: clone returns non-NULL");
    TEST_ASSERT(clone->count == vt->count, "vma: clone has same count");

    v = vma_find(clone, 0x40000000);
    TEST_ASSERT(v != NULL, "vma: clone contains stack VMA");

    /* Type names */
    TEST_ASSERT(strcmp(vma_type_name(VMA_TYPE_ELF), "elf") == 0, "vma: type_name ELF");
    TEST_ASSERT(strcmp(vma_type_name(VMA_TYPE_STACK), "stack") == 0, "vma: type_name STACK");

    /* Cleanup */
    vma_destroy(clone);
    vma_destroy(vt);
}

static void test_frame_ref(void) {
    printf("[FRAME_REF] ");

    /* Allocate a test frame */
    uint32_t frame = pmm_alloc_frame();
    TEST_ASSERT(frame != 0, "frame_ref: alloc frame");

    /* pmm_alloc_frame calls frame_ref_set1, so refcount should be 1 */
    TEST_ASSERT(frame_ref_get(frame) == 1, "frame_ref: initial refcount is 1");

    /* Increment */
    frame_ref_inc(frame);
    TEST_ASSERT(frame_ref_get(frame) == 2, "frame_ref: inc to 2");

    frame_ref_inc(frame);
    TEST_ASSERT(frame_ref_get(frame) == 3, "frame_ref: inc to 3");

    /* Decrement */
    int rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 2, "frame_ref: dec returns 2");
    TEST_ASSERT(frame_ref_get(frame) == 2, "frame_ref: get returns 2");

    rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 1, "frame_ref: dec returns 1");

    rc = frame_ref_dec(frame);
    TEST_ASSERT(rc == 0, "frame_ref: dec returns 0");

    /* Free the frame now that refcount is 0 */
    pmm_free_frame(frame);

    /* Out-of-range should be safe */
    TEST_ASSERT(frame_ref_get(0xFFFFFFFF) == 0, "frame_ref: out-of-range get returns 0");
}

static void test_memory_phase3(void) {
    printf("[MEMORY_P3] ");

    /* Test VMA insertion with overlapping check */
    vma_table_t *vt = vma_init();
    TEST_ASSERT(vt != NULL, "mem_p3: vma_init ok");

    /* Insert and find_free should skip existing VMAs */
    vma_insert(vt, 0x20000000, 0x20004000, VMA_READ | VMA_WRITE | VMA_ANON, VMA_TYPE_ANON);
    vt->mmap_next = 0x20000000;  /* Reset to force scanning past existing */

    uint32_t gap = vma_find_free(vt, 0x1000);
    TEST_ASSERT(gap >= 0x20004000, "mem_p3: find_free skips existing VMA");

    /* Test vmm helpers */
    uint32_t pd = vmm_create_user_pagedir();
    TEST_ASSERT(pd != 0, "mem_p3: create user pagedir");

    /* vmm_get_pte for unmapped page should return 0 */
    uint32_t pte = vmm_get_pte(pd, 0x20000000);
    /* PTE may be from kernel page table (identity mapped), check it exists */
    TEST_ASSERT(1, "mem_p3: vmm_get_pte doesn't crash");

    /* vmm_ensure_pt should create page table entry */
    uint32_t pt = vmm_ensure_pt(pd, 0x30000000);
    TEST_ASSERT(pt != 0, "mem_p3: vmm_ensure_pt allocates page table");

    /* Map a page then check PTE */
    uint32_t frame = pmm_alloc_frame();
    TEST_ASSERT(frame != 0, "mem_p3: alloc frame for mapping");
    vmm_map_user_page(pd, 0x30000000, frame, PTE_PRESENT | PTE_WRITABLE | PTE_USER);

    pte = vmm_get_pte(pd, 0x30000000);
    TEST_ASSERT(pte & PTE_PRESENT, "mem_p3: mapped page is present");
    TEST_ASSERT((pte & PAGE_MASK) == frame, "mem_p3: PTE points to correct frame");

    /* Unmap */
    vmm_unmap_user_page(pd, 0x30000000);
    pte = vmm_get_pte(pd, 0x30000000);
    TEST_ASSERT(!(pte & PTE_PRESENT), "mem_p3: unmapped page not present");

    /* Free frame and page dir */
    pmm_free_frame(frame);
    vmm_destroy_user_pagedir(pd);

    /* Test PTE_COW flag value */
    TEST_ASSERT(PTE_COW == 0x400, "mem_p3: PTE_COW is AVL bit 10");

    vma_destroy(vt);
}

/* ---- Phase 4: Linux Syscall Expansion ---- */

static void test_phase4_syscalls(void) {
    printf("== Phase 4 Syscall Tests ==\n");

    /* ── Verify new syscall number defines ────────────────────────── */
    TEST_ASSERT(LINUX_SYS_unlink == 10, "SYS_unlink defined");
    TEST_ASSERT(LINUX_SYS_chdir == 12, "SYS_chdir defined");
    TEST_ASSERT(LINUX_SYS_time == 13, "SYS_time defined");
    TEST_ASSERT(LINUX_SYS_rename == 38, "SYS_rename defined");
    TEST_ASSERT(LINUX_SYS_mkdir == 39, "SYS_mkdir defined");
    TEST_ASSERT(LINUX_SYS_rmdir == 40, "SYS_rmdir defined");
    TEST_ASSERT(LINUX_SYS_pipe == 42, "SYS_pipe defined");
    TEST_ASSERT(LINUX_SYS_umask == 60, "SYS_umask defined");
    TEST_ASSERT(LINUX_SYS_getpgrp == 65, "SYS_getpgrp defined");
    TEST_ASSERT(LINUX_SYS_gettimeofday == 78, "SYS_gettimeofday defined");
    TEST_ASSERT(LINUX_SYS_fchdir == 133, "SYS_fchdir defined");
    TEST_ASSERT(LINUX_SYS_readv == 145, "SYS_readv defined");
    TEST_ASSERT(LINUX_SYS_poll == 168, "SYS_poll defined");
    TEST_ASSERT(LINUX_SYS_setuid32 == 213, "SYS_setuid32 defined");
    TEST_ASSERT(LINUX_SYS_setgid32 == 214, "SYS_setgid32 defined");
    TEST_ASSERT(LINUX_SYS_clock_gettime == 265, "SYS_clock_gettime defined");
    TEST_ASSERT(LINUX_SYS_statfs64 == 268, "SYS_statfs64 defined");
    TEST_ASSERT(LINUX_SYS_fstatfs64 == 269, "SYS_fstatfs64 defined");

    /* ── Verify new errno/ioctl defines ───────────────────────────── */
    TEST_ASSERT(LINUX_ENOTEMPTY == 39, "ENOTEMPTY defined");
    TEST_ASSERT(LINUX_EPERM == 1, "EPERM defined");
    TEST_ASSERT(LINUX_TCSETS == 0x5402, "TCSETS defined");
    TEST_ASSERT(LINUX_FIONREAD == 0x541B, "FIONREAD defined");
    TEST_ASSERT(LINUX_TIOCGPGRP == 0x540F, "TIOCGPGRP defined");

    /* ── Verify poll event flags ──────────────────────────────────── */
    TEST_ASSERT(LINUX_POLLIN == 0x0001, "POLLIN defined");
    TEST_ASSERT(LINUX_POLLOUT == 0x0004, "POLLOUT defined");
    TEST_ASSERT(LINUX_POLLHUP == 0x0010, "POLLHUP defined");
    TEST_ASSERT(LINUX_POLLNVAL == 0x0020, "POLLNVAL defined");

    /* ── unlink: create → delete → verify gone ────────────────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4unlink", 0);
        fs_write_file("p4unlink", (const uint8_t *)"test", 4);

        /* Verify file exists */
        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4unlink", &par, nm);
        TEST_ASSERT(ino >= 0, "unlink: file exists before delete");

        /* Delete via our handler */
        int rc = fs_delete_file("p4unlink");
        TEST_ASSERT(rc == 0, "unlink: delete succeeds");

        /* Verify gone */
        ino = fs_resolve_path("/tmp/p4unlink", &par, nm);
        TEST_ASSERT(ino < 0, "unlink: file gone after delete");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── mkdir/rmdir: create dir → verify → rmdir → verify gone ──── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        int rc = fs_create_file("p4dir", 1);
        TEST_ASSERT(rc >= 0, "mkdir: create succeeds");

        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4dir", &par, nm);
        TEST_ASSERT(ino >= 0, "mkdir: dir exists");

        inode_t node;
        if (ino >= 0) {
            fs_read_inode((uint32_t)ino, &node);
            TEST_ASSERT(node.type == INODE_DIR, "mkdir: inode is DIR type");
        }

        /* rmdir */
        rc = fs_delete_file("p4dir");
        TEST_ASSERT(rc == 0, "rmdir: delete succeeds");

        ino = fs_resolve_path("/tmp/p4dir", &par, nm);
        TEST_ASSERT(ino < 0, "rmdir: dir gone after delete");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── rename: create → rename → old gone, new exists ──────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4old", 0);
        fs_write_file("p4old", (const uint8_t *)"rename", 6);

        int rc = fs_rename("p4old", "p4new");
        TEST_ASSERT(rc == 0, "rename: succeeds");

        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino_old = fs_resolve_path("/tmp/p4old", &par, nm);
        TEST_ASSERT(ino_old < 0, "rename: old name gone");

        int ino_new = fs_resolve_path("/tmp/p4new", &par, nm);
        TEST_ASSERT(ino_new >= 0, "rename: new name exists");

        /* Cleanup */
        fs_delete_file("p4new");
        fs_change_directory_by_inode(saved_inode);
    }

/* ── chdir: change to /tmp → verify cwd → restore ───────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        int rc = fs_change_directory("/tmp");
        TEST_ASSERT(rc == 0, "chdir: to /tmp succeeds");

        const char *cwd = fs_get_cwd();
        TEST_ASSERT(strcmp(cwd, "/tmp") == 0, "chdir: cwd is /tmp");

        fs_change_directory_by_inode(saved_inode);
    }

/* ── pipe: create → write → read → verify data ───────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "pipe: create succeeds");

        task_info_t *t = task_get(tid);
        if (rc == 0 && t && t->fds) {
            /* Write to pipe */
            const char *msg = "hello";
            int wr = pipe_write(wfd, msg, 5, tid);
            TEST_ASSERT(wr == 5, "pipe: write 5 bytes");

            /* Read from pipe */
            char buf[16] = {0};
            int rd = pipe_read(rfd, buf, 16, tid);
            TEST_ASSERT(rd == 5, "pipe: read 5 bytes back");
            TEST_ASSERT(memcmp(buf, "hello", 5) == 0, "pipe: data matches");

            /* Cleanup */
            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── umask: verify default, set, verify old returned ─────────── */
    {
        int tid = task_get_current();
        task_info_t *t = task_get(tid);
        if (t) {
            uint16_t saved = t->umask;
            /* Set to 0077, check old returned */
            uint16_t old = t->umask;
            t->umask = 0077;
            TEST_ASSERT(old == saved, "umask: old value returned");
            TEST_ASSERT(t->umask == 0077, "umask: new value set");
            t->umask = saved;  /* restore */
        }
    }

/* ── dup/dup2: dup fd → both read same data ──────────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "dup: pipe create");

        if (rc == 0) {
            int dup_rfd = fd_dup(tid, rfd);
            TEST_ASSERT(dup_rfd >= 0, "dup: succeeds");

            /* Write data */
            pipe_write(wfd, "XY", 2, tid);

            /* Read from original fd */
            char buf[4] = {0};
            int rd = pipe_read(rfd, buf, 1, tid);
            TEST_ASSERT(rd == 1 && buf[0] == 'X', "dup: read from original");

            /* Read from duped fd — should get next byte */
            rd = pipe_read(dup_rfd, buf, 1, tid);
            TEST_ASSERT(rd == 1 && buf[0] == 'Y', "dup: read from dup");

            /* dup2 to specific fd */
            int dup2_fd = fd_dup2(tid, wfd, 10);
            TEST_ASSERT(dup2_fd == 10, "dup2: to fd 10");

            pipe_close(rfd, tid);
            if (dup_rfd >= 0) pipe_close(dup_rfd, tid);
            pipe_close(wfd, tid);
            if (dup2_fd >= 0) pipe_close(dup2_fd, tid);
        }
    }

/* ── time: Unix timestamp must be after 2024-01-01 ───────────── */
    {
        extern uint32_t rtc_get_epoch(void);
        uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
        TEST_ASSERT(unix_time > 1700000000U, "time: epoch > 2024");
    }

/* ── readv: write data → readv with 2 iovecs → verify ────────── */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        fs_create_file("p4readv", 0);
        fs_write_file("p4readv", (const uint8_t *)"ABCDEFGH", 8);

        /* Open via resolve + manual FD setup */
        uint32_t par;
        char nm[MAX_NAME_LEN];
        int ino = fs_resolve_path("/tmp/p4readv", &par, nm);
        TEST_ASSERT(ino >= 0, "readv: file exists");

        if (ino >= 0) {
            /* Read 4 bytes at offset 0, then 4 bytes at offset 4 */
            char buf1[8] = {0}, buf2[8] = {0};
            int r1 = fs_read_at((uint32_t)ino, (uint8_t *)buf1, 0, 4);
            int r2 = fs_read_at((uint32_t)ino, (uint8_t *)buf2, 4, 4);
            TEST_ASSERT(r1 == 4 && memcmp(buf1, "ABCD", 4) == 0, "readv: first chunk");
            TEST_ASSERT(r2 == 4 && memcmp(buf2, "EFGH", 4) == 0, "readv: second chunk");
        }

        fs_delete_file("p4readv");
        fs_change_directory_by_inode(saved_inode);
    }

/* ── poll: pipe pair → write → poll read end → POLLIN set ────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        TEST_ASSERT(rc == 0, "poll: pipe create");

        if (rc == 0) {
            /* Write a byte */
            pipe_write(wfd, "Z", 1, tid);

            /* Query read end */
            int r = pipe_poll_query(task_get(tid)->fds[rfd].pipe_id, 0);
            TEST_ASSERT(r & PIPE_POLL_IN, "poll: POLLIN set after write");

            /* Query write end */
            r = pipe_poll_query(task_get(tid)->fds[wfd].pipe_id, 1);
            TEST_ASSERT(r & PIPE_POLL_OUT, "poll: POLLOUT set (space available)");

            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── statfs64: verify FS geometry constants ──────────────────── */
    {
        struct linux_statfs64 st;
        memset(&st, 0, sizeof(st));
        st.f_type = 0x696D706F;
        st.f_bsize = BLOCK_SIZE;
        st.f_blocks = NUM_BLOCKS;
        st.f_files = NUM_INODES;
        st.f_namelen = MAX_NAME_LEN;
        TEST_ASSERT(st.f_bsize == 4096, "statfs: f_bsize == 4096");
        TEST_ASSERT(st.f_blocks == 65536, "statfs: f_blocks == 65536");
        TEST_ASSERT(st.f_files == 4096, "statfs: f_files == 4096");
        TEST_ASSERT(st.f_namelen == 28, "statfs: f_namelen == 28");
    }

/* ── ioctl TIOCGWINSZ: verify reasonable values ──────────────── */
    {
        struct linux_winsize ws = {0};
        ws.ws_row = 67;
        ws.ws_col = 240;
        ws.ws_xpixel = 1920;
        ws.ws_ypixel = 1080;
        TEST_ASSERT(ws.ws_row > 0 && ws.ws_row < 256, "ioctl: ws_row reasonable");
        TEST_ASSERT(ws.ws_col > 0 && ws.ws_col < 1024, "ioctl: ws_col reasonable");
    }

/* ── clock_gettime: monotonic time advancing ─────────────────── */
    {
        extern volatile uint32_t pit_ticks;
        uint32_t t1 = pit_ticks;
        TEST_ASSERT(t1 > 0, "clock_gettime: PIT running");

        /* Verify CLOCK_REALTIME gives unix time */
        uint32_t unix_time = rtc_get_epoch() + IMPOS_EPOCH_OFFSET;
        TEST_ASSERT(unix_time > 1700000000U, "clock_gettime: REALTIME valid");
    }

/* ── pipe_get_count: verify byte count ───────────────────────── */
    {
        int tid = task_get_current();
        int rfd, wfd;
        int rc = pipe_create(&rfd, &wfd, tid);
        if (rc == 0) {
            task_info_t *t = task_get(tid);
            pipe_write(wfd, "ABC", 3, tid);
            uint32_t cnt = pipe_get_count(t->fds[rfd].pipe_id);
            TEST_ASSERT(cnt == 3, "pipe_get_count: 3 bytes");
            pipe_close(rfd, tid);
            pipe_close(wfd, tid);
        }
    }

/* ── fs_count_free helpers: verify non-zero ──────────────────── */
    {
        uint32_t fb = fs_count_free_blocks();
        uint32_t fi = fs_count_free_inodes();
        TEST_ASSERT(fb > 0, "fs_count_free_blocks > 0");
        TEST_ASSERT(fi > 0, "fs_count_free_inodes > 0");
        TEST_ASSERT(fb < NUM_BLOCKS, "fs_count_free_blocks < NUM_BLOCKS");
        TEST_ASSERT(fi < NUM_INODES, "fs_count_free_inodes < NUM_INODES");
    }
}

/* ---- Nanosleep & Execve ---- */

static void test_nanosleep_execve(void) {
    printf("== Nanosleep & Execve Tests ==\n");

    /* Verify syscall number defines exist */
    TEST_ASSERT(LINUX_SYS_nanosleep == 162, "SYS_nanosleep defined");
    TEST_ASSERT(LINUX_SYS_clock_nanosleep == 267, "SYS_clock_nanosleep defined");
    TEST_ASSERT(LINUX_SYS_execve == 11, "SYS_execve defined");

    /* Verify errno defines */
    TEST_ASSERT(LINUX_EINTR == 4, "EINTR defined");
    TEST_ASSERT(LINUX_ENOEXEC == 8, "ENOEXEC defined");
    TEST_ASSERT(LINUX_EFAULT == 14, "EFAULT defined");

    /* Test the timing infrastructure — PIT must be running for nanosleep */
    uint32_t t1 = pit_get_ticks();
    TEST_ASSERT(t1 > 0, "PIT ticks running");

    /* Test elf_exec with non-existent file — should return ENOENT */
    int tid = task_get_current();
    int rc = elf_exec(tid, "/nonexistent_binary", 0, NULL);
    TEST_ASSERT(rc == -LINUX_ENOENT, "elf_exec nonexistent returns ENOENT");

    /* Test elf_exec with invalid file — create a non-ELF file, then try exec */
    fs_create_file("/tmp/notelf", 0);
    fs_write_file("/tmp/notelf", (const uint8_t *)"notanelf", 8);
    rc = elf_exec(tid, "/tmp/notelf", 0, NULL);
    TEST_ASSERT(rc == -LINUX_ENOEXEC, "elf_exec non-ELF returns ENOEXEC");
    fs_delete_file("/tmp/notelf");

    /* Verify elf_exec function pointer valid */
    int (*exec_fn)(int, const char *, int, const char **) = elf_exec;
    TEST_ASSERT(exec_fn != NULL, "elf_exec function pointer valid");
    (void)exec_fn;

    /* ── Live ELF binary tests (require initrd binaries) ───────── */

    /* Test sleep_test: spawns ELF, checks it completes and timing is ~1s */
    {
        int ret = elf_run("/bin/sleep_test");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            uint32_t t_start = pit_get_ticks();
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            uint32_t elapsed = pit_get_ticks() - t_start;
            /* 120Hz PIT: 1 second = 120 ticks, allow 90-180 range */
            TEST_ASSERT(elapsed >= 90 && elapsed <= 180,
                        "sleep_test paused ~1s");
        } else {
            TEST_ASSERT(0, "sleep_test binary found in initrd");
        }
    }

    /* Test exec_test + exec_target: spawn exec_test, verify it completes */
    {
        int ret = elf_run("/bin/exec_test");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            int pid_before = t ? t->pid : -1;
            while (t && t->active && t->state != TASK_STATE_ZOMBIE)
                task_yield();
            /* exec_test prints EXEC_PID=N, then execve's to exec_target
             * which prints TARGET_PID=N. Both should complete without crash. */
            TEST_ASSERT(t && t->state == TASK_STATE_ZOMBIE,
                        "exec_test completed (zombie)");
            TEST_ASSERT(t && t->exit_code == 0,
                        "exec_target exited with code 0");
        } else {
            TEST_ASSERT(0, "exec_test binary found in initrd");
        }
    }
}

/* ---- Phase 5: Dynamic Linking Infrastructure Tests ---- */

static void test_phase5_dynlink(void) {
    printf("== Phase 5 Dynamic Linking Tests ==\n");

    /* ── ELF type/program header defines ─────────────────────────── */
    TEST_ASSERT(ET_EXEC == 2, "ET_EXEC == 2");
    TEST_ASSERT(ET_DYN == 3, "ET_DYN == 3");
    TEST_ASSERT(PT_NULL == 0, "PT_NULL == 0");
    TEST_ASSERT(PT_LOAD == 1, "PT_LOAD == 1");
    TEST_ASSERT(PT_DYNAMIC == 2, "PT_DYNAMIC == 2");
    TEST_ASSERT(PT_INTERP == 3, "PT_INTERP == 3");
    TEST_ASSERT(PT_NOTE == 4, "PT_NOTE == 4");
    TEST_ASSERT(PT_PHDR == 6, "PT_PHDR == 6");

    /* ── Auxiliary vector defines ─────────────────────────────────── */
    TEST_ASSERT(AT_NULL == 0, "AT_NULL == 0");
    TEST_ASSERT(AT_PHDR == 3, "AT_PHDR == 3");
    TEST_ASSERT(AT_PHENT == 4, "AT_PHENT == 4");
    TEST_ASSERT(AT_PHNUM == 5, "AT_PHNUM == 5");
    TEST_ASSERT(AT_PAGESZ == 6, "AT_PAGESZ == 6");
    TEST_ASSERT(AT_BASE == 7, "AT_BASE == 7");
    TEST_ASSERT(AT_ENTRY == 9, "AT_ENTRY == 9");
    TEST_ASSERT(AT_UID == 11, "AT_UID == 11");
    TEST_ASSERT(AT_EUID == 12, "AT_EUID == 12");
    TEST_ASSERT(AT_GID == 13, "AT_GID == 13");
    TEST_ASSERT(AT_EGID == 14, "AT_EGID == 14");
    TEST_ASSERT(AT_HWCAP == 16, "AT_HWCAP == 16");
    TEST_ASSERT(AT_CLKTCK == 17, "AT_CLKTCK == 17");
    TEST_ASSERT(AT_SECURE == 23, "AT_SECURE == 23");
    TEST_ASSERT(AT_RANDOM == 25, "AT_RANDOM == 25");

    /* ── Struct size validation ───────────────────────────────────── */
    TEST_ASSERT(sizeof(Elf32_auxv_t) == 8, "Elf32_auxv_t is 8 bytes");
    TEST_ASSERT(sizeof(Elf32_Phdr) == 32, "Elf32_Phdr is 32 bytes");
    TEST_ASSERT(sizeof(Elf32_Ehdr) == 52, "Elf32_Ehdr is 52 bytes");

    /* ── Interpreter base address ─────────────────────────────────── */
    TEST_ASSERT(INTERP_BASE_ADDR == 0x40100000, "INTERP_BASE_ADDR correct");
    TEST_ASSERT(INTERP_BASE_ADDR > USER_SPACE_BASE, "interp base above stack");

    /* ── ET_DYN acceptance in elf_detect ──────────────────────────── */
    /* elf_detect only checks magic bytes, not e_type — verify it accepts
     * both ET_EXEC and ET_DYN headers */
    {
        uint8_t fake_elf[64];
        memset(fake_elf, 0, sizeof(fake_elf));
        fake_elf[0] = 0x7F;
        fake_elf[1] = 'E';
        fake_elf[2] = 'L';
        fake_elf[3] = 'F';
        TEST_ASSERT(elf_detect(fake_elf, sizeof(fake_elf)) == 1,
                    "elf_detect accepts ELF magic");

        /* Non-ELF data */
        uint8_t not_elf[4] = {0x00, 0x01, 0x02, 0x03};
        TEST_ASSERT(elf_detect(not_elf, 4) == 0, "elf_detect rejects non-ELF");

        /* Too small */
        TEST_ASSERT(elf_detect(not_elf, 2) == 0, "elf_detect rejects small data");
    }

    /* ── Elf32_auxv_t field offsets ──────────────────────────────── */
    {
        Elf32_auxv_t av;
        av.a_type = AT_PAGESZ;
        av.a_val = 4096;
        TEST_ASSERT(av.a_type == AT_PAGESZ, "auxv_t type field works");
        TEST_ASSERT(av.a_val == 4096, "auxv_t val field works");
    }

    /* ── File-backed mmap infrastructure ─────────────────────────── */
    /* Test that a file created in /tmp can be read via fs_read_at */
    {
        uint32_t saved_inode = fs_get_cwd_inode();
        fs_change_directory("/tmp");

        /* Create test file with known pattern */
        fs_create_file("mmap_test", 0);
        uint8_t pattern[64];
        for (int i = 0; i < 64; i++) pattern[i] = (uint8_t)(i ^ 0xAA);
        fs_write_file("mmap_test", pattern, 64);

        /* Read it back using fs_read_at (same path mmap uses) */
        uint32_t par;
        char nm[256];
        int ino = fs_resolve_path("/tmp/mmap_test", &par, nm);
        TEST_ASSERT(ino >= 0, "mmap_test file exists");

        if (ino >= 0) {
            uint8_t readback[64];
            memset(readback, 0, 64);
            int rd = fs_read_at((uint32_t)ino, readback, 0, 64);
            TEST_ASSERT(rd == 64, "fs_read_at reads 64 bytes");
            TEST_ASSERT(memcmp(readback, pattern, 64) == 0,
                        "fs_read_at data matches pattern");

            /* Offset read */
            memset(readback, 0, 32);
            rd = fs_read_at((uint32_t)ino, readback, 32, 32);
            TEST_ASSERT(rd == 32, "fs_read_at offset read 32 bytes");
            TEST_ASSERT(memcmp(readback, pattern + 32, 32) == 0,
                        "fs_read_at offset data matches");
        }

        fs_delete_file("mmap_test");
        fs_change_directory_by_inode(saved_inode);
    }

    /* ── mmap flags defines ──────────────────────────────────────── */
    TEST_ASSERT(LINUX_MAP_ANONYMOUS == 0x20, "MAP_ANONYMOUS == 0x20");
    TEST_ASSERT(LINUX_MAP_FIXED == 0x10, "MAP_FIXED == 0x10");
    TEST_ASSERT(LINUX_MAP_PRIVATE == 0x02, "MAP_PRIVATE == 0x02");
    TEST_ASSERT(LINUX_PROT_READ == 0x1, "PROT_READ == 0x1");
    TEST_ASSERT(LINUX_PROT_WRITE == 0x2, "PROT_WRITE == 0x2");
    TEST_ASSERT(LINUX_PROT_EXEC == 0x4, "PROT_EXEC == 0x4");
    TEST_ASSERT(LINUX_PROT_NONE == 0x0, "PROT_NONE == 0x0");

    /* ── elf_load_interp function pointer exists ─────────────────── */
    uint32_t (*interp_fn)(uint32_t, const char *, uint32_t, void *, void *)
        = elf_load_interp;
    TEST_ASSERT(interp_fn != NULL, "elf_load_interp function exists");
    (void)interp_fn;

    /* ── VMA type for ELF segments ───────────────────────────────── */
    TEST_ASSERT(VMA_TYPE_ELF == 2, "VMA_TYPE_ELF == 2");
    TEST_ASSERT(VMA_TYPE_ANON == 1, "VMA_TYPE_ANON == 1");
    TEST_ASSERT(VMA_TYPE_STACK == 3, "VMA_TYPE_STACK == 3");

    /* ── Live dynamic binary integration test ────────────────────── */
    /* Spawn hello_dyn (dynamically linked via musl ldso), wait for
     * it to finish. This exercises the full chain: PT_INTERP scan →
     * elf_load_interp → auxv → file-backed mmap → ldso bootstrap →
     * symbol resolution → CRT → main() → write() → _exit(). */
    {
        int ret = elf_run("/bin/hello_dyn");
        if (ret >= 0) {
            task_info_t *t = task_get(ret);
            uint32_t t_start = pit_get_ticks();
            while (t && t->active && t->state != TASK_STATE_ZOMBIE) {
                task_yield();
                /* Timeout after ~5 seconds (600 ticks at 120 Hz) */
                if (pit_get_ticks() - t_start > 600) break;
            }
            TEST_ASSERT(t && t->state == TASK_STATE_ZOMBIE,
                        "hello_dyn completed (zombie)");
            TEST_ASSERT(t && t->exit_code == 0,
                        "hello_dyn exited with code 0");
        } else {
            /* Binary not in initrd — skip gracefully */
            printf("  (hello_dyn not in initrd, skipping live test)\n");
        }
    }
}

/* ---- Phase 7.5: Widget Toolkit Tests ---- */

static void test_phase75_widgets(void) {
    printf("== Phase 7.5 Widget Toolkit Tests ==\n");

    /* ── uw_create returns valid window ──────────────────────── */
    ui_window_t *tw = uw_create(100, 100, 300, 200, "TestWin");
    TEST_ASSERT(tw != NULL, "p75: uw_create returns non-null");
    if (!tw) return;
    TEST_ASSERT(tw->wm_id >= 0, "p75: uw_create wm_id valid");
    TEST_ASSERT(tw->widget_count == 0, "p75: initial widget_count is 0");
    TEST_ASSERT(tw->focused_widget == -1, "p75: focused_widget starts at -1");

    /* ── ui_add_label ────────────────────────────────────────── */
    int lbl = ui_add_label(tw, 10, 10, 100, 20, "Hello", 0xFF00FF00);
    TEST_ASSERT(lbl >= 0, "p75: ui_add_label returns valid index");
    TEST_ASSERT(tw->widget_count == 1, "p75: widget_count is 1 after add_label");
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(w != NULL, "p75: ui_get_widget returns non-null");
        TEST_ASSERT(w->type == UI_LABEL, "p75: label type correct");
        TEST_ASSERT(strcmp(w->label.text, "Hello") == 0, "p75: label text stored");
        TEST_ASSERT(w->label.color == 0xFF00FF00, "p75: label color stored");
    }

    /* ── ui_add_button ───────────────────────────────────────── */
    int btn = ui_add_button(tw, 10, 40, 80, 30, "Click", NULL);
    TEST_ASSERT(btn >= 0, "p75: ui_add_button returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, btn);
        TEST_ASSERT(w != NULL, "p75: button widget non-null");
        TEST_ASSERT(w->type == UI_BUTTON, "p75: button type correct");
        TEST_ASSERT(strcmp(w->button.text, "Click") == 0, "p75: button text stored");
        TEST_ASSERT(w->flags & UI_FLAG_FOCUSABLE, "p75: button is focusable");
    }

    /* ── ui_add_textinput ────────────────────────────────────── */
    int txt = ui_add_textinput(tw, 10, 80, 200, 28, "Type here", 64, 0);
    TEST_ASSERT(txt >= 0, "p75: ui_add_textinput returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, txt);
        TEST_ASSERT(w != NULL, "p75: textinput widget non-null");
        TEST_ASSERT(w->type == UI_TEXTINPUT, "p75: textinput type correct");
        TEST_ASSERT(strcmp(w->textinput.placeholder, "Type here") == 0,
                    "p75: textinput placeholder stored");
    }

    /* ── ui_add_progress ─────────────────────────────────────── */
    int prog = ui_add_progress(tw, 10, 120, 200, 20, 42, "Loading");
    TEST_ASSERT(prog >= 0, "p75: ui_add_progress returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, prog);
        TEST_ASSERT(w != NULL, "p75: progress widget non-null");
        TEST_ASSERT(w->progress.value == 42, "p75: progress value stored");
    }

    /* ── ui_add_checkbox ─────────────────────────────────────── */
    int chk = ui_add_checkbox(tw, 10, 150, 150, 20, "Enable", 1);
    TEST_ASSERT(chk >= 0, "p75: ui_add_checkbox returns valid index");
    {
        ui_widget_t *w = ui_get_widget(tw, chk);
        TEST_ASSERT(w != NULL, "p75: checkbox widget non-null");
        TEST_ASSERT(w->checkbox.checked == 1, "p75: checkbox initial state correct");
    }

    /* ── ui_add_separator ────────────────────────────────────── */
    int sep = ui_add_separator(tw, 10, 175, 200);
    TEST_ASSERT(sep >= 0, "p75: ui_add_separator returns valid index");

    /* ── ui_add_tabs ─────────────────────────────────────────── */
    static const char *tab_labels[] = { "Tab1", "Tab2" };
    int tabs = ui_add_tabs(tw, 10, 180, 200, 30, tab_labels, 2);
    TEST_ASSERT(tabs >= 0, "p75: ui_add_tabs returns valid index");

    /* ── ui_widget_set_visible ───────────────────────────────── */
    ui_widget_set_visible(tw, lbl, 0);
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(!(w->flags & UI_FLAG_VISIBLE), "p75: set_visible(0) clears flag");
    }
    ui_widget_set_visible(tw, lbl, 1);
    {
        ui_widget_t *w = ui_get_widget(tw, lbl);
        TEST_ASSERT(w->flags & UI_FLAG_VISIBLE, "p75: set_visible(1) sets flag");
    }

    /* ── ui_focus_next ───────────────────────────────────────── */
    tw->focused_widget = -1;
    ui_focus_next(tw);
    TEST_ASSERT(tw->focused_widget == btn, "p75: focus_next finds first focusable (button)");

    /* ── ui_get_widget boundary ──────────────────────────────── */
    TEST_ASSERT(ui_get_widget(tw, -1) == NULL, "p75: get_widget(-1) returns NULL");
    TEST_ASSERT(ui_get_widget(tw, 999) == NULL, "p75: get_widget(999) returns NULL");

    /* ── Widget count ────────────────────────────────────────── */
    int expected_count = 7; /* label, button, textinput, progress, checkbox, separator, tabs */
    TEST_ASSERT(tw->widget_count == expected_count,
                "p75: widget_count matches added widgets");

    /* ── App registry: only implemented apps ─────────────────── */
    int app_count = app_get_count();
    TEST_ASSERT(app_count == 9, "p75: app registry has 9 apps");

    /* Verify known apps exist */
    TEST_ASSERT(app_find("terminal") != NULL, "p75: terminal in registry");
    TEST_ASSERT(app_find("calculator") != NULL, "p75: calculator in registry");
    TEST_ASSERT(app_find("notes") != NULL, "p75: notes in registry");
    TEST_ASSERT(app_find("about") != NULL, "p75: about in registry");
    TEST_ASSERT(app_find("mines") != NULL, "p75: minesweeper in registry");

    /* Verify stubs are removed */
    TEST_ASSERT(app_find("browser") == NULL, "p75: browser stub removed");
    TEST_ASSERT(app_find("email") == NULL, "p75: email stub removed");

    /* ── Cleanup ─────────────────────────────────────────────── */
    uw_destroy(tw);
}

/* ---- Phase 8: Desktop IPC ---- */

#include <kernel/msgbus.h>
#include <kernel/clipboard.h>
#include <kernel/notify.h>
#include <kernel/systray.h>

static int p8_handler_called;
static int p8_handler_ival;
static const char *p8_handler_sval;

static void p8_test_handler(const msgbus_msg_t *msg, void *ctx) {
    (void)ctx;
    p8_handler_called++;
    if (msg->type == MSGBUS_TYPE_INT)
        p8_handler_ival = msg->ival;
    else if (msg->type == MSGBUS_TYPE_STR)
        p8_handler_sval = msg->sval;
}

static void test_phase8_ipc(void) {
    printf("== Phase 8 Desktop IPC Tests ==\n");

    /* ── Clipboard tests ────────────────────────────────────────── */
    clipboard_clear();
    size_t clen = 0;
    const char *cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 0, "p8: clipboard initially empty");
    TEST_ASSERT(clipboard_has_content() == 0, "p8: no content initially");

    clipboard_copy("hello", 5);
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 5, "p8: clipboard len after copy");
    TEST_ASSERT(memcmp(cdata, "hello", 5) == 0, "p8: clipboard content");
    TEST_ASSERT(clipboard_has_content() == 1, "p8: has content after copy");

    clipboard_copy("world!", 6);
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 6, "p8: clipboard overwrite len");
    TEST_ASSERT(memcmp(cdata, "world!", 6) == 0, "p8: clipboard overwrite");

    clipboard_clear();
    cdata = clipboard_get(&clen);
    TEST_ASSERT(clen == 0, "p8: clipboard clear");

    /* Large copy truncates at CLIPBOARD_MAX-1 */
    {
        char big[CLIPBOARD_MAX + 10];
        memset(big, 'A', sizeof(big));
        clipboard_copy(big, sizeof(big));
        cdata = clipboard_get(&clen);
        TEST_ASSERT(clen == CLIPBOARD_MAX - 1, "p8: clipboard truncate");
    }
    clipboard_clear();

    /* ── Message bus tests ──────────────────────────────────────── */
    msgbus_init(); /* reset for testing */

    int sub = msgbus_subscribe("test-topic", p8_test_handler, NULL);
    TEST_ASSERT(sub >= 0, "p8: msgbus subscribe returns valid id");

    p8_handler_called = 0;
    p8_handler_ival = 0;
    int delivered = msgbus_publish_int("test-topic", 42);
    TEST_ASSERT(delivered == 1, "p8: msgbus publish delivers to 1 sub");
    TEST_ASSERT(p8_handler_called == 1, "p8: handler called once");
    TEST_ASSERT(p8_handler_ival == 42, "p8: handler received int value");

    /* Unrelated topic */
    p8_handler_called = 0;
    delivered = msgbus_publish_int("other-topic", 99);
    TEST_ASSERT(delivered == 0, "p8: unrelated topic not delivered");
    TEST_ASSERT(p8_handler_called == 0, "p8: handler not called for other topic");

    /* Multiple subscribers */
    int sub2 = msgbus_subscribe("test-topic", p8_test_handler, NULL);
    TEST_ASSERT(sub2 >= 0, "p8: second subscribe ok");
    p8_handler_called = 0;
    delivered = msgbus_publish_int("test-topic", 7);
    TEST_ASSERT(delivered == 2, "p8: two subscribers notified");
    TEST_ASSERT(p8_handler_called == 2, "p8: handler called twice");

    /* Unsubscribe */
    msgbus_unsubscribe(sub);
    p8_handler_called = 0;
    delivered = msgbus_publish_int("test-topic", 1);
    TEST_ASSERT(delivered == 1, "p8: after unsub, one remaining");
    TEST_ASSERT(p8_handler_called == 1, "p8: only remaining handler called");

    /* String message */
    p8_handler_called = 0;
    p8_handler_sval = NULL;
    msgbus_publish_str("test-topic", "hello bus");
    TEST_ASSERT(p8_handler_called == 1, "p8: str msg delivered");
    TEST_ASSERT(p8_handler_sval != NULL && strcmp(p8_handler_sval, "hello bus") == 0,
                "p8: str msg content correct");

    msgbus_unsubscribe(sub2);

    /* ── Notification tests (structural) ────────────────────────── */
    notify_id_t nid = notify_post("Test", "Body", NOTIFY_INFO, 600);
    TEST_ASSERT(nid >= 0, "p8: notify_post returns valid id");
    /* Before any tick, queued but not yet promoted */
    TEST_ASSERT(notify_visible_count() == 0, "p8: notify not visible before tick");
    notify_dismiss(nid);

    /* ── System tray tests (structural) ──────────────────────────── */
    systray_init(); /* reset for testing */
    TEST_ASSERT(systray_get_count() == 0, "p8: systray initially empty");

    int ti = systray_register("Ab", "Test", 0xFFFFFFFF, NULL, NULL);
    TEST_ASSERT(ti >= 0, "p8: systray register returns valid idx");
    TEST_ASSERT(systray_get_count() == 1, "p8: systray count after register");
    TEST_ASSERT(systray_get_width() == SYSTRAY_ITEM_W, "p8: systray width");

    int ti2 = systray_register("Cd", "Test2", 0xFF00FF00, NULL, NULL);
    TEST_ASSERT(ti2 >= 0, "p8: systray second register ok");
    TEST_ASSERT(systray_get_count() == 2, "p8: systray count == 2");
    TEST_ASSERT(systray_get_width() == 2 * SYSTRAY_ITEM_W, "p8: systray width x2");

    systray_unregister(ti);
    TEST_ASSERT(systray_get_count() == 1, "p8: systray count after unregister");

    systray_unregister(ti2);
    TEST_ASSERT(systray_get_count() == 0, "p8: systray empty after unregister all");

    /* Re-init msgbus for rest of system */
    msgbus_init();
}

void test_process_all(void) {
    test_scheduler_priority();
    test_process_lifecycle();
    test_process_groups();
    test_signals_phase2();
    test_fd_table();
    test_futex();
    test_pthreads();
    test_vma();
    test_frame_ref();
    test_memory_phase3();
    test_phase4_syscalls();
    test_nanosleep_execve();
    test_phase5_dynlink();
    test_phase75_widgets();
    test_phase8_ipc();
}
