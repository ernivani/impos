/*
 * aarch64 kernel_main — Phase 3.
 *
 * Initializes: serial → PMM → VMM (MMU) → interrupts → tasks → scheduler.
 * Creates test threads to verify preemptive multitasking.
 */

#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/sched.h>

/* Global boot info */
boot_info_t g_boot_info;

/* ═══════════════════════════════════════════════════════════════════
 * Test threads — demonstrate preemptive multitasking
 * ═══════════════════════════════════════════════════════════════════ */

static void test_thread_a(void) {
    int count = 0;
    while (1) {
        if (count % 120 == 0) {
            serial_printf("[Thread A] count=%d tick=%u\r\n",
                         count, pit_get_ticks());
        }
        count++;
        /* Busy work — will be preempted by timer */
        for (volatile int i = 0; i < 50000; i++);
    }
}

static void test_thread_b(void) {
    int count = 0;
    while (1) {
        if (count % 120 == 0) {
            serial_printf("[Thread B] count=%d tick=%u\r\n",
                         count, pit_get_ticks());
        }
        count++;
        for (volatile int i = 0; i < 50000; i++);
    }
}

static void test_thread_c(void) {
    /* Short-lived thread: prints 5 messages then exits */
    for (int i = 0; i < 5; i++) {
        serial_printf("[Thread C] message %d/5, tick=%u\r\n",
                     i + 1, pit_get_ticks());
        pit_sleep_ms(500);
    }
    serial_printf("[Thread C] exiting.\r\n");
    task_exit();
}

void kernel_main(void *dtb) {
    /* Save DTB pointer for later parsing (Phase 8) */
    g_boot_info.arch_data = dtb;

    /* PL011 is pre-initialized by QEMU, but run our init anyway */
    serial_init();

    /* Initialize terminal (PL011 UART output) */
    terminal_initialize();

    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts("  ImposOS aarch64 — Phase 3\r\n");
    serial_puts("  QEMU virt, Cortex-A72, 8GB RAM\r\n");
    serial_puts("========================================\r\n");
    serial_puts("\r\n");

    DBG("DTB pointer: 0x%x", (unsigned)(uintptr_t)dtb);

    /* Phase 2: Physical Memory Manager */
    pmm_init(0);

    /* Phase 2: Virtual Memory Manager (enables MMU) */
    vmm_init(0);

    /* Phase 2: Interrupts (GICv2 + Generic Timer at 120Hz) */
    idt_initialize();

    /* Phase 3: Task Manager */
    task_init();
    DBG("Task subsystem initialized (%d tasks)", task_count());

    /* Phase 3: Scheduler */
    sched_init();
    DBG("Scheduler active");

    /* Create test threads */
    int tid_a = task_create_thread("thread-A", test_thread_a, 1);
    int tid_b = task_create_thread("thread-B", test_thread_b, 1);
    int tid_c = task_create_thread("thread-C", test_thread_c, 1);

    DBG("Created threads: A=%d B=%d C=%d", tid_a, tid_b, tid_c);
    DBG("Entering cooperative idle loop (boot task)...");

    /* The boot task (TASK_KERNEL) runs cooperatively on the boot stack.
     * When the timer fires and scheduler activates, it may preempt to
     * one of the threads above. When all threads yield/sleep, control
     * returns here via the cooperative path in schedule(). */
    uint32_t last_print = 0;
    while (1) {
        uint32_t ticks = pit_get_ticks();
        if (ticks - last_print >= 600) {  /* Every 5 seconds */
            DBG("[Boot] tick=%u threads=%d free_frames=%u",
                ticks, task_count(), pmm_free_frame_count());
            last_print = ticks;
        }
        cpu_halting = 1;
        __asm__ volatile("wfi");
        cpu_halting = 0;
    }
}
