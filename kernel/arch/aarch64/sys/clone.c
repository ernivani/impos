/* clone.c — aarch64 stub
 *
 * The full clone() implementation is deeply x86-specific: it pushes
 * i386 register frames (IRET stack frames, segment registers, etc.)
 * onto the child's kernel stack. A proper aarch64 version would use
 * aarch64 exception return frames (SPSR_EL1, ELR_EL1, x0-x30).
 * Stubbed for now — returns -ENOSYS.
 */
#include <kernel/idt.h>
#include <kernel/task.h>
#include <kernel/linux_syscall.h>
#include <kernel/io.h>

int sys_clone(uint32_t flags, uint32_t child_stack, registers_t *parent_regs) {
    (void)flags;
    (void)child_stack;
    (void)parent_regs;
    DBG("clone: not implemented on aarch64");
    return -38;  /* -ENOSYS */
}
