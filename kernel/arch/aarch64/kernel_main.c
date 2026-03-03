/*
 * aarch64 stub kernel_main — Phase 0-1.
 *
 * Prints a greeting to the PL011 UART and halts.
 * This will be replaced by the real kernel_main once enough
 * subsystems are ported (Phase 3+).
 */

#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>

/* Global boot info (defined here for now, will move to common code) */
boot_info_t g_boot_info;

/* Stub for pit_get_ticks — no timer yet (Phase 2 adds the generic timer) */
uint32_t pit_get_ticks(void) {
    return 0;
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
    serial_puts("  Hello from ImposOS aarch64!\r\n");
    serial_puts("  QEMU virt machine, Cortex-A53\r\n");
    serial_puts("========================================\r\n");
    serial_puts("\r\n");

    DBG("DTB pointer: 0x%x", (unsigned)(uintptr_t)dtb);
    DBG("Phase 0 complete — boot stub working.");
    DBG("Next: Phase 2 (interrupts, timer, MMU)");

    /* Halt */
    while (1) {
        __asm__ volatile("wfe");
    }
}
