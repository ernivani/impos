/*
 * aarch64 boot stub for ImposOS on QEMU virt machine.
 *
 * When QEMU loads with -kernel, the CPU is in EL1, MMU off, caches off.
 *   x0 = pointer to DTB (device tree blob)
 *
 * We save the DTB pointer, set up a stack, zero BSS, and call kernel_main.
 */

.section .text.boot
.global _start
_start:
    /* x0 = DTB pointer from QEMU — save to callee-saved register */
    mov     x19, x0

    /* Set up stack pointer (grows downward) */
    ldr     x1, =stack_top
    mov     sp, x1

    /* Zero BSS section */
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
1:  cmp     x1, x2
    b.ge    2f
    str     xzr, [x1], #8
    b       1b
2:

    /* Call global constructors */
    bl      _init

    /* Call kernel_main(dtb_pointer) */
    mov     x0, x19
    bl      kernel_main

    /* If kernel_main returns, halt */
3:  wfe
    b       3b

/* ── Stack (64 KiB, in BSS) ────────────────────────────────────── */
.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:
