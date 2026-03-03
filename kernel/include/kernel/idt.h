#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H

#include <stdint.h>

/* ── Architecture-specific register frame ───────────────────────── */

#if defined(__aarch64__)

/* Saved by exception_stubs.S SAVE_CONTEXT macro.
 * Layout must match the assembly exactly (288 bytes total). */
typedef struct {
    uint64_t x[31];     /* x0-x30 (x30 = LR) */
    uint64_t sp;        /* Pre-exception stack pointer */
    uint64_t elr;       /* Exception Link Register (return address) */
    uint64_t spsr;      /* Saved Program Status Register */
    uint64_t _pad;      /* Padding to 288 bytes (16-byte aligned) */
} registers_t;

#else /* i386 */

/* Registers pushed by isr_common */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss; /* pushed by CPU */
} registers_t;

#endif

/* IRQ handler function pointer */
typedef void (*irq_handler_t)(registers_t* regs);

/* Initialize GDT, IDT, PIC, PIT */
void idt_initialize(void);

/* Register a handler for an IRQ (0-15) */
void irq_register_handler(int irq, irq_handler_t handler);

/* PIT timer functions */
uint32_t pit_get_ticks(void);
void pit_sleep_ms(uint32_t ms);

/* CPU usage tracking */
void pit_get_cpu_stats(uint32_t *idle, uint32_t *busy);
extern volatile int cpu_halting;

/* TSS: update kernel stack pointer for ring 3→0 transitions */
void tss_set_esp0(uint32_t esp0);

/* Update GDT entry 6 base for per-thread FS segment (TEB) */
void gdt_set_fs_base(uint32_t base);

/* Update GDT entry 6 base for per-thread GS segment (Linux TLS) */
void gdt_set_gs_base(uint32_t base);

#endif
