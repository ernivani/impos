#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/config.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/signal.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* From getchar.c — push raw scancode to keyboard ring buffer */
extern void keyboard_push_scancode(uint8_t scancode);

/* ========== GDT ========== */

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt_entries[6];
static gdt_ptr_t gdt_ptr;

/* ========== TSS ========== */

typedef struct {
    uint32_t prev_tss;
    uint32_t esp0;
    uint32_t ss0;
    uint32_t esp1, ss1;
    uint32_t esp2, ss2;
    uint32_t cr3;
    uint32_t eip, eflags;
    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs;
    uint32_t ldt;
    uint16_t trap;
    uint16_t iomap_base;
} __attribute__((packed)) tss_entry_t;

static tss_entry_t tss;

void tss_set_esp0(uint32_t esp0) {
    tss.esp0 = esp0;
}

static void gdt_set_entry(int idx, uint32_t base, uint32_t limit,
                           uint8_t access, uint8_t gran) {
    gdt_entries[idx].base_low    = base & 0xFFFF;
    gdt_entries[idx].base_mid    = (base >> 16) & 0xFF;
    gdt_entries[idx].base_high   = (base >> 24) & 0xFF;
    gdt_entries[idx].limit_low   = limit & 0xFFFF;
    gdt_entries[idx].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt_entries[idx].access      = access;
}

static void gdt_install(void) {
    gdt_set_entry(0, 0, 0, 0, 0);                /* Null segment */
    gdt_set_entry(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Code: ring 0, exec/read */
    gdt_set_entry(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Data: ring 0, read/write */
    gdt_set_entry(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* Code: ring 3, exec/read → 0x1B */
    gdt_set_entry(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* Data: ring 3, read/write → 0x23 */

    /* TSS descriptor → selector 0x28 */
    memset(&tss, 0, sizeof(tss));
    tss.ss0 = 0x10;          /* Kernel data segment for ring 3→0 */
    tss.esp0 = 0;            /* Updated by scheduler on context switch */
    tss.iomap_base = sizeof(tss);  /* No I/O bitmap */

    uint32_t tss_base = (uint32_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    gdt_set_entry(5, tss_base, tss_limit, 0x89, 0x00);

    gdt_ptr.limit = sizeof(gdt_entries) - 1;
    gdt_ptr.base  = (uint32_t)&gdt_entries;

    /* Load GDT and reload segment registers */
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "mov $0x10, %%ax\n\t"  /* Data segment selector */
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp $0x08, $1f\n\t"  /* Code segment selector */
        "1:\n\t"
        : : "r"(&gdt_ptr) : "ax"
    );

    /* Load TSS register */
    __asm__ volatile ("ltr %%ax" : : "a"(0x28));
}

/* ========== IDT ========== */

typedef struct {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t base_hi;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

#define IDT_ENTRIES 256
static idt_entry_t idt_entries[IDT_ENTRIES];
static idt_ptr_t idt_ptr;

static void idt_set_gate(int idx, uint32_t base, uint16_t sel, uint8_t flags) {
    idt_entries[idx].base_lo = base & 0xFFFF;
    idt_entries[idx].base_hi = (base >> 16) & 0xFFFF;
    idt_entries[idx].sel     = sel;
    idt_entries[idx].always0 = 0;
    idt_entries[idx].flags   = flags;
}

/* External ISR stubs from isr_stubs.S */
extern void isr0(void);  extern void isr1(void);  extern void isr2(void);
extern void isr3(void);  extern void isr4(void);  extern void isr5(void);
extern void isr6(void);  extern void isr7(void);  extern void isr8(void);
extern void isr9(void);  extern void isr10(void); extern void isr11(void);
extern void isr12(void); extern void isr13(void); extern void isr14(void);
extern void isr15(void); extern void isr16(void); extern void isr17(void);
extern void isr18(void); extern void isr19(void); extern void isr20(void);
extern void isr21(void); extern void isr22(void); extern void isr23(void);
extern void isr24(void); extern void isr25(void); extern void isr26(void);
extern void isr27(void); extern void isr28(void); extern void isr29(void);
extern void isr30(void); extern void isr31(void);

extern void irq0(void);  extern void irq1(void);  extern void irq2(void);
extern void irq3(void);  extern void irq4(void);  extern void irq5(void);
extern void irq6(void);  extern void irq7(void);  extern void irq8(void);
extern void irq9(void);  extern void irq10(void); extern void irq11(void);
extern void irq12(void); extern void irq13(void); extern void irq14(void);
extern void irq15(void);

extern void isr128(void);  /* INT 0x80: yield/schedule */

/* ========== PIC ========== */

#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xA0
#define PIC2_DATA 0xA1

static void pic_remap(void) {
    /* Save masks */
    uint8_t mask1 = inb(PIC1_DATA);
    uint8_t mask2 = inb(PIC2_DATA);

    /* ICW1: begin initialization in cascade mode */
    outb(PIC1_CMD, 0x11); io_wait();
    outb(PIC2_CMD, 0x11); io_wait();

    /* ICW2: set vector offset */
    outb(PIC1_DATA, 0x20); io_wait(); /* IRQ 0-7  → INT 32-39 */
    outb(PIC2_DATA, 0x28); io_wait(); /* IRQ 8-15 → INT 40-47 */

    /* ICW3: master/slave wiring */
    outb(PIC1_DATA, 0x04); io_wait(); /* Slave on IRQ2 */
    outb(PIC2_DATA, 0x02); io_wait(); /* Cascade identity */

    /* ICW4: 8086 mode */
    outb(PIC1_DATA, 0x01); io_wait();
    outb(PIC2_DATA, 0x01); io_wait();

    /* Restore masks */
    outb(PIC1_DATA, mask1);
    outb(PIC2_DATA, mask2);
}

/* ========== PIT ========== */

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_FREQ     1193182
#define TARGET_HZ    120
#define PIT_DIVISOR   (PIT_FREQ / TARGET_HZ)  /* ~9943 */

volatile uint32_t pit_ticks = 0;
static volatile uint32_t pit_idle_ticks = 0;
static volatile uint32_t pit_busy_ticks = 0;
volatile int cpu_halting = 0;

static void pit_init(void) {
    uint16_t divisor = PIT_DIVISOR;
    outb(PIT_CMD, 0x36);  /* Channel 0, lobyte/hibyte, mode 3 (square wave) */
    outb(PIT_CHANNEL0, divisor & 0xFF);
    outb(PIT_CHANNEL0, (divisor >> 8) & 0xFF);
}

uint32_t pit_get_ticks(void) {
    return pit_ticks;
}

void pit_sleep_ms(uint32_t ms) {
    if (sched_is_active()) {
        /* Preemptive mode: mark task as sleeping and yield */
        int tid = task_get_current();
        task_info_t *t = task_get(tid);
        if (t && (t->stack_base || t->is_user)) {
            /* Thread with its own stack: use proper sleep */
            t->sleep_until = pit_ticks + (ms * TARGET_HZ / 1000) + 1;
            t->state = TASK_STATE_SLEEPING;
            task_yield();
            return;
        }
        /* Cooperative task (no own stack): fall through to busy-wait */
    }

    /* Legacy/boot mode or cooperative task: busy-wait with HLT */
    int saved_task = task_get_current();
    task_set_current(TASK_IDLE);
    uint32_t target = pit_ticks + (ms * TARGET_HZ / 1000);
    if (ms * TARGET_HZ % 1000) target++;
    while ((int32_t)(pit_ticks - target) < 0) {
        cpu_halting = 1;
        __asm__ volatile ("hlt");
    }
    cpu_halting = 0;
    task_set_current(saved_task);
}

void pit_get_cpu_stats(uint32_t *idle, uint32_t *busy) {
    *idle = pit_idle_ticks;
    *busy = pit_busy_ticks;
}

/* ========== IRQ Handler Table ========== */

#define NUM_IRQS 16
static irq_handler_t irq_handlers[NUM_IRQS];

void irq_register_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < NUM_IRQS)
        irq_handlers[irq] = handler;
}

/* PIT IRQ0 handler */
static int second_counter = 0;

static void pit_handler(registers_t* regs) {
    (void)regs;
    pit_ticks++;
    if (cpu_halting) {
        pit_idle_ticks++;
    } else {
        pit_busy_ticks++;
    }
    task_tick();
    second_counter++;
    if (second_counter >= TARGET_HZ) {
        second_counter = 0;
        config_tick_second();
        task_sample();
    }
}

/* Keyboard IRQ1 handler — read scancode from port 0x60 and push to ring buffer.
   Check status register bit 5 to filter out mouse data (auxiliary flag). */
static void keyboard_irq_handler(registers_t* regs) {
    (void)regs;
    uint8_t status = inb(0x64);
    if (!(status & 0x01)) return;       /* No data available */
    uint8_t scancode = inb(0x60);
    if (status & 0x20) return;          /* Mouse data — discard */
    keyboard_push_scancode(scancode);
}

/* C-level ISR dispatcher, called from isr_common.
   Returns (possibly different) stack pointer for context switching. */
registers_t* isr_handler(registers_t* regs) {
    uint32_t int_no = regs->int_no;

    if (int_no >= 32 && int_no < 48) {
        /* IRQ */
        int irq = int_no - 32;

        if (irq_handlers[irq])
            irq_handlers[irq](regs);

        /* Send EOI */
        if (irq >= 8)
            outb(PIC2_CMD, 0x20);
        outb(PIC1_CMD, 0x20);

        /* Timer IRQ: invoke scheduler for context switching */
        if (irq == 0)
            regs = schedule(regs);
    } else if (int_no == 0x80) {
        /* Syscall: dispatch based on EAX, may invoke scheduler */
        regs = syscall_handler(regs);
    } else if (int_no < 32) {
        /* CPU Exception — map to signal */
        int signum = 0;
        const char *name = NULL;
        switch (int_no) {
            case 0:  signum = SIGFPE;  name = "Division by zero"; break;
            case 6:  signum = SIGILL;  name = "Invalid opcode";   break;
            case 8:  signum = SIGBUS;  name = "Double fault";     break;
            case 13: signum = SIGSEGV; name = "General protection fault"; break;
            case 14: signum = SIGSEGV; name = "Page fault";       break;
            default: break; /* INT 1-5, 7, 9-12, 15-31: ignore */
        }

        if (signum) {
            int tid = task_get_current();
            task_info_t *t = task_get_raw(tid);

            /* Read CR2 for page faults */
            uint32_t cr2 = 0;
            if (int_no == 14)
                __asm__ volatile ("mov %%cr2, %0" : "=r"(cr2));

            /* Diagnostic output to serial and console */
            serial_printf("[EXCEPTION] %s (INT %d) in task %d '%s'\n",
                          name, int_no, tid, (t && t->name[0]) ? t->name : "?");
            serial_printf("  EIP=0x%x CS=0x%x ERR=0x%x EFLAGS=0x%x\n",
                          regs->eip, regs->cs, regs->err_code, regs->eflags);
            serial_printf("  EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\n",
                          regs->eax, regs->ebx, regs->ecx, regs->edx);
            serial_printf("  ESP=0x%x EBP=0x%x ESI=0x%x EDI=0x%x\n",
                          regs->esp, regs->ebp, regs->esi, regs->edi);
            if (int_no == 14) {
                uint32_t err = regs->err_code;
                serial_printf("  CR2=0x%x [%s %s %s%s%s]\n", cr2,
                              (err & 1) ? "protection" : "not-present",
                              (err & 2) ? "write" : "read",
                              (err & 4) ? "user" : "kernel",
                              (err & 8) ? " reserved-bit" : "",
                              (err & 16) ? " instruction-fetch" : "");
            }

            /* Check if this task is recoverable */
            int recoverable = t && tid >= 4 && t->killable &&
                              (t->stack_base || t->is_user);

            if (recoverable) {
                printf("%s in task '%s' (PID %d) — killed\n",
                       name, t->name, t->pid);

                if (t->is_user && !t->sig.in_handler) {
                    /* Ring 3 user thread: set pending signal for delivery */
                    t->sig.pending |= (1 << signum);
                    /* Skip faulting instruction won't help — let sig_deliver
                       handle it (default action = kill, or user handler) */
                } else {
                    /* Kernel thread, or user handler itself faulted:
                       force-kill immediately and reschedule */
                    sig_send(tid, SIGKILL);
                    regs = schedule(regs);
                }
            } else {
                /* Core task (0-3) or non-recoverable: halt system */
                printf("\n=== %s (INT %d) in core task %d ===\n", name, int_no, tid);
                printf("EIP=0x%x CS=0x%x ERR=0x%x EFLAGS=0x%x\n",
                       regs->eip, regs->cs, regs->err_code, regs->eflags);
                printf("EAX=0x%x EBX=0x%x ECX=0x%x EDX=0x%x\n",
                       regs->eax, regs->ebx, regs->ecx, regs->edx);
                printf("ESP=0x%x EBP=0x%x ESI=0x%x EDI=0x%x\n",
                       regs->esp, regs->ebp, regs->esi, regs->edi);
                if (int_no == 14)
                    printf("CR2=0x%x\n", cr2);
                printf("System halted.\n");
                __asm__ volatile ("cli; hlt");
            }
        }
    }

    /* Deliver pending signals before returning to user mode */
    if ((regs->cs & 0x3) == 3) {
        int tid = task_get_current();
        if (sig_deliver(tid, regs))
            regs = schedule(regs);
    }

    return regs;
}

/* ========== CMOS RTC ========== */

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

static uint8_t bcd_to_bin(uint8_t val) {
    return (val >> 4) * 10 + (val & 0x0F);
}

void rtc_read_datetime(datetime_t *dt) {
    /* Wait until CMOS not updating */
    while (cmos_read(0x0A) & 0x80);

    uint8_t sec  = cmos_read(0x00);
    uint8_t min  = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t day  = cmos_read(0x07);
    uint8_t mon  = cmos_read(0x08);
    uint8_t year = cmos_read(0x09);
    uint8_t regB = cmos_read(0x0B);

    /* Convert from BCD if needed */
    if (!(regB & 0x04)) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7F) | (hour & 0x80);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
    }

    /* Convert 12h to 24h if needed */
    if (!(regB & 0x02) && (hour & 0x80)) {
        hour = ((hour & 0x7F) + 12) % 24;
    }

    dt->second = sec;
    dt->minute = min;
    dt->hour   = hour;
    dt->day    = day;
    dt->month  = mon;
    dt->year   = 2000 + year;
}

/* ========== Initialize ========== */

void idt_initialize(void) {
    /* Install GDT */
    gdt_install();

    /* Remap PIC */
    pic_remap();

    /* Clear IDT */
    memset(idt_entries, 0, sizeof(idt_entries));

    /* Set exception gates (ISR 0-31) */
    idt_set_gate(0,  (uint32_t)isr0,  0x08, 0x8E);
    idt_set_gate(1,  (uint32_t)isr1,  0x08, 0x8E);
    idt_set_gate(2,  (uint32_t)isr2,  0x08, 0x8E);
    idt_set_gate(3,  (uint32_t)isr3,  0x08, 0x8E);
    idt_set_gate(4,  (uint32_t)isr4,  0x08, 0x8E);
    idt_set_gate(5,  (uint32_t)isr5,  0x08, 0x8E);
    idt_set_gate(6,  (uint32_t)isr6,  0x08, 0x8E);
    idt_set_gate(7,  (uint32_t)isr7,  0x08, 0x8E);
    idt_set_gate(8,  (uint32_t)isr8,  0x08, 0x8E);
    idt_set_gate(9,  (uint32_t)isr9,  0x08, 0x8E);
    idt_set_gate(10, (uint32_t)isr10, 0x08, 0x8E);
    idt_set_gate(11, (uint32_t)isr11, 0x08, 0x8E);
    idt_set_gate(12, (uint32_t)isr12, 0x08, 0x8E);
    idt_set_gate(13, (uint32_t)isr13, 0x08, 0x8E);
    idt_set_gate(14, (uint32_t)isr14, 0x08, 0x8E);
    idt_set_gate(15, (uint32_t)isr15, 0x08, 0x8E);
    idt_set_gate(16, (uint32_t)isr16, 0x08, 0x8E);
    idt_set_gate(17, (uint32_t)isr17, 0x08, 0x8E);
    idt_set_gate(18, (uint32_t)isr18, 0x08, 0x8E);
    idt_set_gate(19, (uint32_t)isr19, 0x08, 0x8E);
    idt_set_gate(20, (uint32_t)isr20, 0x08, 0x8E);
    idt_set_gate(21, (uint32_t)isr21, 0x08, 0x8E);
    idt_set_gate(22, (uint32_t)isr22, 0x08, 0x8E);
    idt_set_gate(23, (uint32_t)isr23, 0x08, 0x8E);
    idt_set_gate(24, (uint32_t)isr24, 0x08, 0x8E);
    idt_set_gate(25, (uint32_t)isr25, 0x08, 0x8E);
    idt_set_gate(26, (uint32_t)isr26, 0x08, 0x8E);
    idt_set_gate(27, (uint32_t)isr27, 0x08, 0x8E);
    idt_set_gate(28, (uint32_t)isr28, 0x08, 0x8E);
    idt_set_gate(29, (uint32_t)isr29, 0x08, 0x8E);
    idt_set_gate(30, (uint32_t)isr30, 0x08, 0x8E);
    idt_set_gate(31, (uint32_t)isr31, 0x08, 0x8E);

    /* Set IRQ gates (INT 32-47) */
    idt_set_gate(32, (uint32_t)irq0,  0x08, 0x8E);
    idt_set_gate(33, (uint32_t)irq1,  0x08, 0x8E);
    idt_set_gate(34, (uint32_t)irq2,  0x08, 0x8E);
    idt_set_gate(35, (uint32_t)irq3,  0x08, 0x8E);
    idt_set_gate(36, (uint32_t)irq4,  0x08, 0x8E);
    idt_set_gate(37, (uint32_t)irq5,  0x08, 0x8E);
    idt_set_gate(38, (uint32_t)irq6,  0x08, 0x8E);
    idt_set_gate(39, (uint32_t)irq7,  0x08, 0x8E);
    idt_set_gate(40, (uint32_t)irq8,  0x08, 0x8E);
    idt_set_gate(41, (uint32_t)irq9,  0x08, 0x8E);
    idt_set_gate(42, (uint32_t)irq10, 0x08, 0x8E);
    idt_set_gate(43, (uint32_t)irq11, 0x08, 0x8E);
    idt_set_gate(44, (uint32_t)irq12, 0x08, 0x8E);
    idt_set_gate(45, (uint32_t)irq13, 0x08, 0x8E);
    idt_set_gate(46, (uint32_t)irq14, 0x08, 0x8E);
    idt_set_gate(47, (uint32_t)irq15, 0x08, 0x8E);

    /* INT 0x80: syscall gate (DPL=3 so ring 3 can invoke) */
    idt_set_gate(0x80, (uint32_t)isr128, 0x08, 0xEE);

    /* Load IDT */
    idt_ptr.limit = sizeof(idt_entries) - 1;
    idt_ptr.base  = (uint32_t)&idt_entries;
    __asm__ volatile ("lidt (%0)" : : "r"(&idt_ptr));

    /* Register default handlers */
    memset(irq_handlers, 0, sizeof(irq_handlers));
    irq_register_handler(0, pit_handler);
    irq_register_handler(1, keyboard_irq_handler);

    /* Initialize PIT */
    pit_init();

    /* Unmask IRQ0 (PIT), IRQ1 (keyboard), IRQ2 (cascade to slave PIC) */
    outb(PIC1_DATA, 0xF8);  /* 11111000 = IRQ0 + IRQ1 + IRQ2(cascade) */
    /* Unmask IRQ11 (network card) and IRQ12 (PS/2 mouse) on slave PIC */
    outb(PIC2_DATA, 0xE7);  /* 11100111 = IRQ11 + IRQ12 unmasked */

    /* Enable interrupts */
    __asm__ volatile ("sti");
}
