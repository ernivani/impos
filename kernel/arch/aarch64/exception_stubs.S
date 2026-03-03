/*
 * aarch64 exception vector table and context save/restore.
 *
 * VBAR_EL1 must be 2KB-aligned, with 16 entries at 0x80 (128-byte) spacing.
 * Each entry is a small trampoline (saves x0/x1, loads type, branches to
 * shared handler). The full context save/restore lives outside the table.
 *
 * Type encoding for arch_handle_exception(registers_t *regs, uint32_t type):
 *   bits [3:2] = group (0=curr_sp0, 1=curr_spx, 2=lower_a64, 3=lower_a32)
 *   bits [1:0] = kind  (0=sync, 1=irq, 2=fiq, 3=serror)
 */

/* Frame layout: x0-x30 (31×8=248) + sp(8) + elr(8) + spsr(8) + pad(16) = 288 */
#define FRAME_SIZE 288
#define OFF_X0     0
#define OFF_SP     248
#define OFF_ELR    256
#define OFF_SPSR   264

/* ══════════════════════════════════════════════════════════════════
 * Shared handler — entered from each vector trampoline.
 *
 * On entry:
 *   w1     = exception type code
 *   [sp]   = saved {x0, x1} from trampoline (16 bytes)
 *   sp+16  = pre-exception stack
 * ══════════════════════════════════════════════════════════════════ */
.section .text

exception_handler_common:
    /* Allocate register frame below the trampoline's 16-byte save area */
    sub     sp, sp, #FRAME_SIZE
    /* Stack layout: [sp..sp+FRAME_SIZE) = frame
     *               [sp+FRAME_SIZE..+16) = trampoline-saved x0,x1 */

    /* Save x2-x29 (untouched by trampoline) */
    stp     x2,  x3,  [sp, #OFF_X0 + 16]
    stp     x4,  x5,  [sp, #OFF_X0 + 32]
    stp     x6,  x7,  [sp, #OFF_X0 + 48]
    stp     x8,  x9,  [sp, #OFF_X0 + 64]
    stp     x10, x11, [sp, #OFF_X0 + 80]
    stp     x12, x13, [sp, #OFF_X0 + 96]
    stp     x14, x15, [sp, #OFF_X0 + 112]
    stp     x16, x17, [sp, #OFF_X0 + 128]
    stp     x18, x19, [sp, #OFF_X0 + 144]
    stp     x20, x21, [sp, #OFF_X0 + 160]
    stp     x22, x23, [sp, #OFF_X0 + 176]
    stp     x24, x25, [sp, #OFF_X0 + 192]
    stp     x26, x27, [sp, #OFF_X0 + 208]
    stp     x28, x29, [sp, #OFF_X0 + 224]
    str     x30,      [sp, #OFF_X0 + 240]

    /* Recover original x0/x1 from trampoline save area → frame slots */
    ldp     x10, x11, [sp, #FRAME_SIZE]
    stp     x10, x11, [sp, #OFF_X0]

    /* Save system registers */
    mrs     x10, elr_el1
    mrs     x11, spsr_el1
    str     x10, [sp, #OFF_ELR]
    str     x11, [sp, #OFF_SPSR]

    /* Save pre-exception SP */
    add     x10, sp, #(FRAME_SIZE + 16)
    str     x10, [sp, #OFF_SP]

    /* Call C handler: arch_handle_exception(regs=x0, type=x1)
     * w1 still holds the type code set by the trampoline.
     * Returns registers_t* in x0 — may be a DIFFERENT frame (context switch). */
    mov     x0, sp
    bl      arch_handle_exception

    /* ── Context switch: adopt returned frame as our restore source ── */
    mov     sp, x0

    /* ── Restore context ───────────────────────────────────────── */

    ldr     x10, [sp, #OFF_ELR]
    ldr     x11, [sp, #OFF_SPSR]
    msr     elr_el1, x10
    msr     spsr_el1, x11

    ldp     x0,  x1,  [sp, #OFF_X0 +  0]
    ldp     x2,  x3,  [sp, #OFF_X0 + 16]
    ldp     x4,  x5,  [sp, #OFF_X0 + 32]
    ldp     x6,  x7,  [sp, #OFF_X0 + 48]
    ldp     x8,  x9,  [sp, #OFF_X0 + 64]
    ldp     x10, x11, [sp, #OFF_X0 + 80]
    ldp     x12, x13, [sp, #OFF_X0 + 96]
    ldp     x14, x15, [sp, #OFF_X0 + 112]
    ldp     x16, x17, [sp, #OFF_X0 + 128]
    ldp     x18, x19, [sp, #OFF_X0 + 144]
    ldp     x20, x21, [sp, #OFF_X0 + 160]
    ldp     x22, x23, [sp, #OFF_X0 + 176]
    ldp     x24, x25, [sp, #OFF_X0 + 192]
    ldp     x26, x27, [sp, #OFF_X0 + 208]
    ldp     x28, x29, [sp, #OFF_X0 + 224]
    ldr     x30,      [sp, #OFF_X0 + 240]

    /* Deallocate frame + trampoline's 16 bytes */
    add     sp, sp, #(FRAME_SIZE + 16)

    eret

/* ══════════════════════════════════════════════════════════════════
 * Vector entry trampoline (must fit in 0x80 = 128 bytes each)
 * Saves x0/x1, loads type into w1, branches to shared handler.
 * 4 instructions × 4 bytes = 16 bytes — well within limit.
 * ══════════════════════════════════════════════════════════════════ */
.macro VECTOR_ENTRY label type
.balign 0x80
\label:
    stp     x0, x1, [sp, #-16]!
    mov     w1, #\type
    b       exception_handler_common
.endm

/* ══════════════════════════════════════════════════════════════════
 * Exception Vector Table (2KB aligned)
 * ══════════════════════════════════════════════════════════════════ */
.balign 2048
.global exception_vector_table
exception_vector_table:

/* Group 0: Current EL with SP_EL0 (not used by our kernel) */
    VECTOR_ENTRY vec_curr_sp0_sync,    0x00
    VECTOR_ENTRY vec_curr_sp0_irq,     0x01
    VECTOR_ENTRY vec_curr_sp0_fiq,     0x02
    VECTOR_ENTRY vec_curr_sp0_serror,  0x03

/* Group 1: Current EL with SP_ELx (kernel mode — timer IRQ fires here) */
    VECTOR_ENTRY vec_curr_spx_sync,    0x04
    VECTOR_ENTRY vec_curr_spx_irq,     0x05
    VECTOR_ENTRY vec_curr_spx_fiq,     0x06
    VECTOR_ENTRY vec_curr_spx_serror,  0x07

/* Group 2: Lower EL using AArch64 (user mode — syscalls fire here) */
    VECTOR_ENTRY vec_lower_a64_sync,   0x08
    VECTOR_ENTRY vec_lower_a64_irq,    0x09
    VECTOR_ENTRY vec_lower_a64_fiq,    0x0A
    VECTOR_ENTRY vec_lower_a64_serror, 0x0B

/* Group 3: Lower EL using AArch32 (not used) */
    VECTOR_ENTRY vec_lower_a32_sync,   0x0C
    VECTOR_ENTRY vec_lower_a32_irq,    0x0D
    VECTOR_ENTRY vec_lower_a32_fiq,    0x0E
    VECTOR_ENTRY vec_lower_a32_serror, 0x0F
