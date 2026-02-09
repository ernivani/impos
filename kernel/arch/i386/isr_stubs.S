/* isr_stubs.S - Exception and IRQ stub entry points */

.section .text

/* Macro for exceptions that push NO error code */
.macro ISR_NOERRCODE num
.global isr\num
isr\num:
    pushl $0        /* dummy error code */
    pushl $\num     /* interrupt number */
    jmp isr_common
.endm

/* Macro for exceptions that push an error code */
.macro ISR_ERRCODE num
.global isr\num
isr\num:
    /* error code already on stack */
    pushl $\num     /* interrupt number */
    jmp isr_common
.endm

/* Macro for IRQ stubs */
.macro IRQ irqnum, intnum
.global irq\irqnum
irq\irqnum:
    pushl $0        /* dummy error code */
    pushl $\intnum  /* interrupt number */
    jmp isr_common
.endm

/* Exception stubs (ISR 0-31) */
ISR_NOERRCODE 0   /* Division By Zero */
ISR_NOERRCODE 1   /* Debug */
ISR_NOERRCODE 2   /* NMI */
ISR_NOERRCODE 3   /* Breakpoint */
ISR_NOERRCODE 4   /* Overflow */
ISR_NOERRCODE 5   /* Bound Range Exceeded */
ISR_NOERRCODE 6   /* Invalid Opcode */
ISR_NOERRCODE 7   /* Device Not Available */
ISR_ERRCODE   8   /* Double Fault */
ISR_NOERRCODE 9   /* Coprocessor Segment Overrun */
ISR_ERRCODE   10  /* Invalid TSS */
ISR_ERRCODE   11  /* Segment Not Present */
ISR_ERRCODE   12  /* Stack-Segment Fault */
ISR_ERRCODE   13  /* General Protection Fault */
ISR_ERRCODE   14  /* Page Fault */
ISR_NOERRCODE 15  /* Reserved */
ISR_NOERRCODE 16  /* x87 FP Exception */
ISR_ERRCODE   17  /* Alignment Check */
ISR_NOERRCODE 18  /* Machine Check */
ISR_NOERRCODE 19  /* SIMD FP Exception */
ISR_NOERRCODE 20  /* Virtualization Exception */
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

/* IRQ stubs (IRQ 0-15 → INT 32-47) */
IRQ 0, 32   /* PIT Timer */
IRQ 1, 33   /* Keyboard */
IRQ 2, 34   /* Cascade */
IRQ 3, 35   /* COM2 */
IRQ 4, 36   /* COM1 */
IRQ 5, 37   /* LPT2 */
IRQ 6, 38   /* Floppy */
IRQ 7, 39   /* LPT1 / Spurious */
IRQ 8, 40   /* RTC */
IRQ 9, 41   /* Free */
IRQ 10, 42  /* Free */
IRQ 11, 43  /* Free / RTL8139 often here */
IRQ 12, 44  /* PS/2 Mouse */
IRQ 13, 45  /* FPU */
IRQ 14, 46  /* Primary ATA */
IRQ 15, 47  /* Secondary ATA */

/* Common ISR handler - saves all regs, calls C handler, restores regs */
.extern isr_handler
isr_common:
    pusha           /* Push edi, esi, ebp, esp, ebx, edx, ecx, eax */
    push %ds
    push %es
    push %fs
    push %gs

    mov $0x10, %ax  /* Load kernel data segment */
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    push %esp       /* Push pointer to registers struct */
    call isr_handler
    add $4, %esp

    pop %gs
    pop %fs
    pop %es
    pop %ds
    popa

    add $8, %esp    /* Remove error code and interrupt number */
    iret
