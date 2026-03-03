/*
 * aarch64 kernel_main — Phase 5.
 *
 * Initializes: serial → PMM → VMM → interrupts → tasks → scheduler → drivers.
 * Tests: block I/O, RTC, PCI scan, shutdown.
 */

#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>
#include <kernel/task.h>
#include <kernel/sched.h>
#include <kernel/syscall.h>
#include <kernel/ata.h>
#include <kernel/rtc.h>
#include <kernel/pci.h>
#include <kernel/mouse.h>
#include <kernel/acpi.h>
#include <string.h>

/* Global boot info */
boot_info_t g_boot_info;

/* ═══════════════════════════════════════════════════════════════════
 * Helper: invoke a syscall from EL1 via SVC #0
 * ═══════════════════════════════════════════════════════════════════ */

static inline int64_t syscall0(uint64_t num) {
    register uint64_t x8 __asm__("x8") = num;
    register int64_t  x0 __asm__("x0");
    __asm__ volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}

static inline int64_t syscall1(uint64_t num, uint64_t a0) {
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t r0 __asm__("x0") = a0;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(x8) : "memory");
    return (int64_t)r0;
}

static inline int64_t syscall3(uint64_t num, uint64_t a0, uint64_t a1, uint64_t a2) {
    register uint64_t x8 __asm__("x8") = num;
    register uint64_t r0 __asm__("x0") = a0;
    register uint64_t r1 __asm__("x1") = a1;
    register uint64_t r2 __asm__("x2") = a2;
    __asm__ volatile("svc #0" : "+r"(r0) : "r"(x8), "r"(r1), "r"(r2) : "memory");
    return (int64_t)r0;
}

/* ═══════════════════════════════════════════════════════════════════
 * Test threads (Phase 4 — kept for regression)
 * ═══════════════════════════════════════════════════════════════════ */

static void test_syscall_write(void) {
    const char msg[] = "[SVC] Hello from SYS_WRITE on aarch64!\r\n";
    syscall3(SYS_WRITE, 1, (uint64_t)(uintptr_t)msg, sizeof(msg) - 1);

    int64_t pid = syscall0(SYS_GETPID);
    serial_printf("[SVC] My PID = %d\r\n", (int)pid);

    serial_printf("[SVC] Sleeping 500ms...\r\n");
    syscall1(SYS_SLEEP, 500);
    serial_printf("[SVC] Woke up at tick=%u\r\n", pit_get_ticks());

    serial_printf("[SVC] Exiting via SYS_EXIT(42)\r\n");
    syscall1(SYS_EXIT, 42);
    while (1) __asm__ volatile("wfi");
}

static void test_yield_loop(void) {
    for (int i = 0; i < 5; i++) {
        serial_printf("[Yield] iteration %d, tick=%u\r\n", i, pit_get_ticks());
        syscall0(SYS_YIELD);
    }
    serial_printf("[Yield] done, exiting\r\n");
    syscall1(SYS_EXIT, 0);
    while (1) __asm__ volatile("wfi");
}

/* ═══════════════════════════════════════════════════════════════════
 * Phase 5: Driver Tests
 * ═══════════════════════════════════════════════════════════════════ */

static void test_block_io(void) {
    serial_puts("\r\n--- Block I/O Test ---\r\n");

    if (ata_initialize() != 0) {
        serial_puts("  [SKIP] No block device found\r\n");
        return;
    }

    /* Read sector 0 (superblock area) */
    static uint8_t sector_buf[512];
    if (ata_read_sectors(0, 1, sector_buf) == 0) {
        serial_puts("  Read sector 0: ");
        for (int i = 0; i < 16; i++)
            serial_printf("%x ", sector_buf[i]);
        serial_puts("\r\n");
    } else {
        serial_puts("  [FAIL] Read sector 0 failed\r\n");
    }

    /* Write/read-back test at a high LBA (sector 100000) */
    static uint8_t write_buf[512];
    static uint8_t read_buf[512];
    for (int i = 0; i < 512; i++)
        write_buf[i] = (uint8_t)(i ^ 0xAA);

    if (ata_write_sectors(100000, 1, write_buf) == 0) {
        memset(read_buf, 0, 512);
        if (ata_read_sectors(100000, 1, read_buf) == 0) {
            int match = (memcmp(write_buf, read_buf, 512) == 0);
            serial_printf("  Write/Read-back @ LBA 100000: %s\r\n",
                         match ? "PASS" : "FAIL");
        } else {
            serial_puts("  [FAIL] Read-back failed\r\n");
        }
    } else {
        serial_puts("  [FAIL] Write failed\r\n");
    }

    /* Flush */
    if (ata_flush() == 0)
        serial_puts("  Flush: OK\r\n");
    else
        serial_puts("  Flush: FAIL\r\n");
}

static void test_rtc(void) {
    serial_puts("\r\n--- RTC Test ---\r\n");

    datetime_t dt;
    rtc_read(&dt);
    serial_printf("  Hardware UTC: %u-%u-%u %u:%u:%u\r\n",
                 dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);

    uint32_t epoch = rtc_get_epoch();
    serial_printf("  Epoch (since 2000): %u seconds\r\n", epoch);
}

static void test_pci(void) {
    serial_puts("\r\n--- PCI Scan ---\r\n");
    pci_scan_bus();
}

void kernel_main(void *dtb) {
    g_boot_info.arch_data = dtb;
    serial_init();
    terminal_initialize();

    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts("  ImposOS aarch64 — Phase 5\r\n");
    serial_puts("  Drivers (VirtIO + PCI + RTC + PSCI)\r\n");
    serial_puts("========================================\r\n");
    serial_puts("\r\n");

    DBG("DTB pointer: 0x%x", (unsigned)(uintptr_t)dtb);

    /* Phase 2: PMM → VMM → Interrupts */
    pmm_init(0);
    vmm_init(0);
    idt_initialize();

    /* Phase 3: Tasks → Scheduler */
    task_init();
    sched_init();
    DBG("Scheduler active, %d tasks", task_count());

    /* Phase 5: Initialize drivers */
    acpi_initialize();
    pci_initialize();
    mouse_initialize();
    rtc_init();

    /* Phase 5: Run driver tests */
    test_block_io();
    test_rtc();
    test_pci();

    /* Phase 4: Test syscalls (regression) */
    int tid_write = task_create_thread("test-write", test_syscall_write, 1);
    int tid_yield = task_create_thread("test-yield", test_yield_loop, 1);
    DBG("Created threads: write=%d yield=%d", tid_write, tid_yield);

    /* Main loop */
    uint32_t last_print = 0;

    while (1) {
        uint32_t ticks = pit_get_ticks();

        if (ticks - last_print >= 600) {
            DBG("[Boot] tick=%u threads=%d free=%u",
                ticks, task_count(), pmm_free_frame_count());
            last_print = ticks;
        }

        cpu_halting = 1;
        __asm__ volatile("wfi");
        cpu_halting = 0;
    }
}
