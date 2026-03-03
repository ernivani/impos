/*
 * aarch64 kernel_main — Phase 2.
 *
 * Initializes: serial → PMM → VMM (MMU) → interrupts (GIC + timer).
 * Then enters an idle loop to demonstrate timer ticks.
 */

#include <kernel/tty.h>
#include <kernel/io.h>
#include <kernel/boot_info.h>
#include <kernel/idt.h>
#include <kernel/pmm.h>
#include <kernel/vmm.h>

/* Global boot info */
boot_info_t g_boot_info;

void kernel_main(void *dtb) {
    /* Save DTB pointer for later parsing (Phase 8) */
    g_boot_info.arch_data = dtb;

    /* PL011 is pre-initialized by QEMU, but run our init anyway */
    serial_init();

    /* Initialize terminal (PL011 UART output) */
    terminal_initialize();

    serial_puts("\r\n");
    serial_puts("========================================\r\n");
    serial_puts("  ImposOS aarch64 — Phase 2\r\n");
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

    /* Verify timer is running — print tick count every ~1 second */
    DBG("Entering idle loop (ticks print every ~120 ticks)...");

    uint32_t last_print = 0;
    while (1) {
        uint32_t ticks = pit_get_ticks();
        if (ticks - last_print >= 120) {
            DBG("tick=%u (uptime: %u.%us, free frames: %u)",
                ticks, ticks / 120, (ticks % 120) * 10 / 120,
                pmm_free_frame_count());
            last_print = ticks;
        }
        cpu_halting = 1;
        __asm__ volatile("wfi");
        cpu_halting = 0;
    }
}
