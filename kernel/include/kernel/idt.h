#ifndef _KERNEL_IDT_H
#define _KERNEL_IDT_H

#include <stdint.h>

/* Registers pushed by isr_common */
typedef struct {
    uint32_t gs, fs, es, ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax; /* pusha */
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags, useresp, ss; /* pushed by CPU */
} registers_t;

/* IRQ handler function pointer */
typedef void (*irq_handler_t)(registers_t* regs);

/* Initialize GDT, IDT, PIC, PIT */
void idt_initialize(void);

/* Register a handler for an IRQ (0-15) */
void irq_register_handler(int irq, irq_handler_t handler);

/* PIT timer functions */
uint32_t pit_get_ticks(void);
void pit_sleep_ms(uint32_t ms);

#endif
