/*
 * aarch64 architecture init — Phase 3.
 *
 * Sets up:
 *   - VBAR_EL1 exception vector table
 *   - GICv2 interrupt controller (Distributor + CPU Interface)
 *   - ARM Generic Timer at 120Hz (matching i386 PIT frequency)
 *   - Exception dispatch (IRQ → handler table, Sync → SVC/page fault)
 *   - Preemptive scheduling via timer IRQ → schedule()
 *
 * QEMU virt hardware map:
 *   GICv2 Distributor:  0x08000000
 *   GICv2 CPU Interface: 0x08010000
 *   PL011 UART:         0x09000000
 *   RAM:                0x40000000+
 */

#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/signal.h>
#include <stdint.h>

/* ═══════════════════════════════════════════════════════════════════
 * GICv2 Register Definitions
 * ═══════════════════════════════════════════════════════════════════ */

/* Distributor (GICD) */
#define GICD_BASE           0x08000000
#define GICD_CTLR           (GICD_BASE + 0x000)  /* Distributor Control */
#define GICD_TYPER          (GICD_BASE + 0x004)  /* Interrupt Controller Type */
#define GICD_ISENABLER(n)   (GICD_BASE + 0x100 + 4*(n))  /* Set-Enable */
#define GICD_ICENABLER(n)   (GICD_BASE + 0x180 + 4*(n))  /* Clear-Enable */
#define GICD_ICPENDR(n)     (GICD_BASE + 0x280 + 4*(n))  /* Clear-Pending */
#define GICD_IPRIORITYR(n)  (GICD_BASE + 0x400 + 4*(n))  /* Priority */
#define GICD_ITARGETSR(n)   (GICD_BASE + 0x800 + 4*(n))  /* Target CPU */
#define GICD_ICFGR(n)       (GICD_BASE + 0xC00 + 4*(n))  /* Config */

/* CPU Interface (GICC) */
#define GICC_BASE           0x08010000
#define GICC_CTLR           (GICC_BASE + 0x000)  /* CPU Interface Control */
#define GICC_PMR            (GICC_BASE + 0x004)  /* Priority Mask */
#define GICC_IAR            (GICC_BASE + 0x00C)  /* Interrupt Acknowledge */
#define GICC_EOIR           (GICC_BASE + 0x010)  /* End of Interrupt */

/* Interrupt IDs */
#define GIC_INTID_SPURIOUS  1023
#define TIMER_PPI           30    /* Non-secure Physical Timer PPI (INTID 30) */

/* ═══════════════════════════════════════════════════════════════════
 * Generic Timer
 * ═══════════════════════════════════════════════════════════════════ */

static uint64_t timer_freq;         /* CNTFRQ_EL0 (typically 62.5 MHz on QEMU) */
static uint64_t timer_interval;     /* Ticks per interrupt (freq / 120 Hz) */
static volatile uint32_t tick_count; /* Monotonic tick counter */

/* CPU usage tracking (matches i386 interface) */
volatile int cpu_halting = 0;
static uint32_t idle_ticks = 0;
static uint32_t busy_ticks = 0;

/* ═══════════════════════════════════════════════════════════════════
 * IRQ handler table (matches i386 interface: 16 slots for IRQ 0-15)
 * ═══════════════════════════════════════════════════════════════════ */

#define MAX_IRQ_HANDLERS 256
static irq_handler_t irq_handlers[MAX_IRQ_HANDLERS];

void irq_register_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < MAX_IRQ_HANDLERS)
        irq_handlers[irq] = handler;
}

/* ═══════════════════════════════════════════════════════════════════
 * Timer system register access
 * ═══════════════════════════════════════════════════════════════════ */

static inline uint64_t read_cntfrq(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v));
    return v;
}

static inline uint64_t read_cntpct(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(v));
    return v;
}

static inline void write_cntp_cval(uint64_t v) {
    __asm__ volatile("msr cntp_cval_el0, %0" :: "r"(v));
}

static inline void write_cntp_ctl(uint32_t v) {
    __asm__ volatile("msr cntp_ctl_el0, %0" :: "r"((uint64_t)v));
}

static inline uint32_t read_cntp_ctl(void) {
    uint64_t v;
    __asm__ volatile("mrs %0, cntp_ctl_el0" : "=r"(v));
    return (uint32_t)v;
}

/* ═══════════════════════════════════════════════════════════════════
 * GICv2 Init
 * ═══════════════════════════════════════════════════════════════════ */

static void gic_init(void) {
    /* Disable distributor while configuring */
    mmio_write32(GICD_CTLR, 0);

    /* How many interrupt lines? ITLinesNumber field [4:0] of TYPER */
    uint32_t typer = mmio_read32(GICD_TYPER);
    uint32_t num_irqs = ((typer & 0x1F) + 1) * 32;
    if (num_irqs > 256) num_irqs = 256;

    /* Disable all interrupts, clear pending */
    for (uint32_t i = 0; i < num_irqs / 32; i++) {
        mmio_write32(GICD_ICENABLER(i), 0xFFFFFFFF);
        mmio_write32(GICD_ICPENDR(i), 0xFFFFFFFF);
    }

    /* Set all SPIs to lowest priority (0xA0) and target CPU 0 */
    for (uint32_t i = 8; i < num_irqs / 4; i++) {
        mmio_write32(GICD_IPRIORITYR(i), 0xA0A0A0A0);
        mmio_write32(GICD_ITARGETSR(i), 0x01010101);
    }

    /* Enable the timer PPI (INTID 30) */
    mmio_write32(GICD_ISENABLER(TIMER_PPI / 32),
                 1 << (TIMER_PPI % 32));

    /* Set timer priority higher (lower value = higher priority) */
    {
        uint32_t reg = TIMER_PPI / 4;
        uint32_t shift = (TIMER_PPI % 4) * 8;
        uint32_t val = mmio_read32(GICD_IPRIORITYR(reg));
        val &= ~(0xFF << shift);
        val |= (0x40 << shift);  /* Priority 0x40 */
        mmio_write32(GICD_IPRIORITYR(reg), val);
    }

    /* Enable distributor (group 0) */
    mmio_write32(GICD_CTLR, 1);

    /* CPU Interface: accept all priorities, enable signaling */
    mmio_write32(GICC_PMR, 0xFF);   /* Priority mask: allow all */
    mmio_write32(GICC_CTLR, 1);     /* Enable CPU interface */
}

/* ═══════════════════════════════════════════════════════════════════
 * Generic Timer Init (120 Hz to match i386 PIT)
 * ═══════════════════════════════════════════════════════════════════ */

static void timer_init(void) {
    timer_freq = read_cntfrq();
    timer_interval = timer_freq / 120;  /* 120 Hz */
    tick_count = 0;

    DBG("Timer: freq=%u Hz, interval=%u ticks (120 Hz)",
        (unsigned)timer_freq, (unsigned)timer_interval);

    /* Set compare value to current count + interval */
    uint64_t now = read_cntpct();
    write_cntp_cval(now + timer_interval);

    /* Enable timer, unmask interrupt */
    write_cntp_ctl(1);  /* ENABLE=1, IMASK=0 */
}

static registers_t* timer_irq_handler(registers_t *regs) {
    tick_count++;

    /* Track CPU usage */
    if (cpu_halting)
        idle_ticks++;
    else
        busy_ticks++;

    /* Task accounting */
    task_tick();

    /* Sample CPU usage every second (120 ticks) */
    if (tick_count % 120 == 0)
        task_sample();

    /* Check signal alarms */
    sig_check_alarms();

    /* Schedule next timer interrupt (absolute, avoids drift) */
    uint64_t next = read_cntpct() + timer_interval;
    write_cntp_cval(next);

    /* Run scheduler — may return a different task's register frame */
    if (sched_is_active())
        return schedule(regs);

    return regs;
}

/* ═══════════════════════════════════════════════════════════════════
 * Timer public API (matches i386 idt.h interface)
 * ═══════════════════════════════════════════════════════════════════ */

uint32_t pit_get_ticks(void) {
    return tick_count;
}

void pit_sleep_ms(uint32_t ms) {
    uint32_t target = tick_count + (ms * 120 + 999) / 1000;
    while (tick_count < target) {
        cpu_halting = 1;
        __asm__ volatile("wfi");
        cpu_halting = 0;
    }
}

void pit_get_cpu_stats(uint32_t *idle, uint32_t *busy) {
    if (idle) *idle = idle_ticks;
    if (busy) *busy = busy_ticks;
}

/* ═══════════════════════════════════════════════════════════════════
 * Stubs for i386-specific functions (not needed on aarch64)
 * ═══════════════════════════════════════════════════════════════════ */

void tss_set_esp0(uint32_t esp0) {
    /* aarch64 uses SP_EL1 automatically for EL0→EL1 transitions.
     * No TSS equivalent needed. This is called from the scheduler
     * on context switch — we might use it for per-task kernel stack
     * tracking later. */
    (void)esp0;
}

void gdt_set_fs_base(uint32_t base) { (void)base; }
void gdt_set_gs_base(uint32_t base) { (void)base; }

/* ═══════════════════════════════════════════════════════════════════
 * Exception Dispatch (called from exception_stubs.S)
 * ═══════════════════════════════════════════════════════════════════ */

/* ESR_EL1 exception class (EC) values */
#define EC_SVC_A64      0x15   /* SVC in AArch64 */
#define EC_DATA_ABORT   0x24   /* Data Abort from lower EL */
#define EC_DATA_ABORT_S 0x25   /* Data Abort from same EL */
#define EC_INST_ABORT   0x20   /* Instruction Abort from lower EL */
#define EC_INST_ABORT_S 0x21   /* Instruction Abort from same EL */

/* Vector type encoding from exception_stubs.S:
 *   bits [3:2] = group (0=curr_sp0, 1=curr_spx, 2=lower_a64, 3=lower_a32)
 *   bits [1:0] = kind  (0=sync, 1=irq, 2=fiq, 3=serror) */
#define VEC_KIND_SYNC   0
#define VEC_KIND_IRQ    1
#define VEC_KIND_FIQ    2
#define VEC_KIND_SERROR 3

registers_t* arch_handle_exception(registers_t *regs, uint32_t type) {
    uint32_t kind = type & 0x3;

    if (kind == VEC_KIND_IRQ || kind == VEC_KIND_FIQ) {
        /* ── IRQ/FIQ: read GIC, dispatch handler ───────────────── */
        uint32_t iar = mmio_read32(GICC_IAR);
        uint32_t intid = iar & 0x3FF;

        if (intid == GIC_INTID_SPURIOUS)
            return regs;

        registers_t *ret = regs;

        /* Timer PPI — may context-switch (returns different regs) */
        if (intid == TIMER_PPI) {
            ret = timer_irq_handler(regs);
        } else if (intid < MAX_IRQ_HANDLERS && irq_handlers[intid]) {
            irq_handlers[intid](regs);
        } else {
            DBG("Unhandled IRQ %u", intid);
        }

        /* End of interrupt */
        mmio_write32(GICC_EOIR, iar);
        return ret;
    }

    if (kind == VEC_KIND_SYNC) {
        /* ── Synchronous: decode ESR_EL1 ───────────────────────── */
        uint64_t esr;
        __asm__ volatile("mrs %0, esr_el1" : "=r"(esr));
        uint32_t ec = (esr >> 26) & 0x3F;

        if (ec == EC_SVC_A64) {
            /* System call — will be wired in Phase 4.
             * For now, task_yield() uses SVC #0 — treat as a yield
             * by triggering a reschedule. */
            if (sched_is_active())
                return schedule(regs);
            return regs;
        }

        if (ec == EC_DATA_ABORT || ec == EC_DATA_ABORT_S ||
            ec == EC_INST_ABORT || ec == EC_INST_ABORT_S) {
            uint64_t far;
            __asm__ volatile("mrs %0, far_el1" : "=r"(far));
            serial_printf("[FAULT] %s abort at addr=0x%x, ELR=0x%x, ESR=0x%x\n",
                         (ec == EC_DATA_ABORT || ec == EC_DATA_ABORT_S) ? "Data" : "Instruction",
                         (unsigned)far, (unsigned)regs->elr, (unsigned)esr);
            /* TODO Phase 6: page fault handler */
            goto hang;
        }

        serial_printf("[EXCEPTION] Unhandled sync EC=0x%x ESR=0x%x ELR=0x%x\n",
                     ec, (unsigned)esr, (unsigned)regs->elr);
        goto hang;
    }

    if (kind == VEC_KIND_SERROR) {
        serial_printf("[SERROR] Asynchronous abort! ELR=0x%x\n",
                     (unsigned)regs->elr);
        goto hang;
    }

    return regs;

hang:
    serial_puts("*** System halted ***\r\n");
    cli();
    while (1) __asm__ volatile("wfe");
}

/* ═══════════════════════════════════════════════════════════════════
 * idt_initialize — main entry point (called from kernel_main)
 * ═══════════════════════════════════════════════════════════════════ */

/* Defined in exception_stubs.S */
extern char exception_vector_table[];

void idt_initialize(void) {
    /* Install exception vector table */
    uint64_t vbar = (uint64_t)(uintptr_t)exception_vector_table;
    __asm__ volatile("msr vbar_el1, %0" :: "r"(vbar));
    __asm__ volatile("isb");

    DBG("VBAR_EL1 = 0x%x", (unsigned)vbar);

    /* Initialize GICv2 */
    gic_init();
    DBG("GICv2 initialized (GICD=0x%x, GICC=0x%x)", GICD_BASE, GICC_BASE);

    /* Initialize timer at 120 Hz */
    timer_init();

    /* Timer handler is called directly from arch_handle_exception,
     * not through the generic IRQ table (since it returns registers_t*
     * for context switching). No registration needed. */

    /* Enable IRQs */
    sti();

    DBG("Interrupts enabled — timer running at 120 Hz");
}
