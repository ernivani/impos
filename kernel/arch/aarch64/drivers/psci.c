/*
 * PSCI Power Management — aarch64
 *
 * Implements the acpi.h API using ARM PSCI calls via HVC.
 * QEMU virt machine supports PSCI 0.2+ with HVC conduit.
 */

#include <kernel/acpi.h>
#include <kernel/io.h>

/* PSCI function IDs (SMC Calling Convention, 32-bit) */
#define PSCI_SYSTEM_OFF   0x84000008
#define PSCI_SYSTEM_RESET 0x84000009

int acpi_initialize(void) {
    /* PSCI is always available on QEMU virt — nothing to probe */
    DBG("PSCI: power management ready (HVC conduit)");
    return 0;
}

void acpi_shutdown(void) {
    DBG("PSCI: SYSTEM_OFF");
    register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_OFF;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory");

    /* Should not return, but halt if it does */
    while (1) __asm__ volatile("wfi");
}

void psci_reboot(void) {
    DBG("PSCI: SYSTEM_RESET");
    register uint64_t x0 __asm__("x0") = PSCI_SYSTEM_RESET;
    __asm__ volatile("hvc #0" : "+r"(x0) :: "memory");
    while (1) __asm__ volatile("wfi");
}
