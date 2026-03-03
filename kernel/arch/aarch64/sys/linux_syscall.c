/*
 * aarch64 Linux Syscall Handler — Phase 4 (stub).
 *
 * aarch64 Linux uses completely different syscall numbers from i386.
 * Full implementation comes in Phase 6 with the ELF64 loader.
 *
 * Convention (aarch64 Linux ABI):
 *   x8  = syscall number
 *   x0-x5 = arguments
 *   x0  = return value (negative = -errno)
 */

#include <kernel/linux_syscall.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/io.h>

registers_t* linux_syscall_handler(registers_t* regs) {
    uint64_t num = regs->x[8];

    /* Log for debugging during bringup */
    DBG("linux_syscall: nr=%u (not yet implemented)", (unsigned)num);

    /* Return -ENOSYS for all unimplemented syscalls */
    regs->x[0] = (uint64_t)(int64_t)(-LINUX_ENOSYS);
    return regs;
}
