/*
 * PCI ECAM (Enhanced Configuration Access Mechanism) — aarch64
 *
 * Implements the pci.h API using MMIO-based ECAM at 0x3F000000.
 * QEMU virt machine maps PCI config space here for bus 0.
 */

#include <kernel/pci.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>

/* ECAM base address for QEMU virt (only present when PCI host is enabled) */
#define PCI_ECAM_BASE  0x3F000000

static int pci_available = 0;

/* Compute ECAM address for bus/device/function/offset */
static inline uintptr_t ecam_addr(uint8_t bus, uint8_t device,
                                  uint8_t function, uint8_t offset) {
    return PCI_ECAM_BASE |
           ((uint32_t)bus << 20) |
           ((uint32_t)device << 15) |
           ((uint32_t)function << 12) |
           (offset & 0xFFF);
}

/* ═══ PCI Config Space Access ══════════════════════════════════ */

uint32_t pci_config_read_dword(uint8_t bus, uint8_t device,
                               uint8_t function, uint8_t offset) {
    if (!pci_available)
        return 0xFFFFFFFF;
    return mmio_read32(ecam_addr(bus, device, function, offset & 0xFC));
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t device,
                              uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t device,
                             uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write_dword(uint8_t bus, uint8_t device,
                            uint8_t function, uint8_t offset, uint32_t value) {
    if (!pci_available) return;
    mmio_write32(ecam_addr(bus, device, function, offset & 0xFC), value);
}

void pci_config_write_word(uint8_t bus, uint8_t device,
                           uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    dword &= ~(0xFFFF << ((offset & 2) * 8));
    dword |= ((uint32_t)value << ((offset & 2) * 8));
    pci_config_write_dword(bus, device, function, offset & 0xFC, dword);
}

void pci_config_write_byte(uint8_t bus, uint8_t device,
                           uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    dword &= ~(0xFF << ((offset & 3) * 8));
    dword |= ((uint32_t)value << ((offset & 3) * 8));
    pci_config_write_dword(bus, device, function, offset & 0xFC, dword);
}

/* ═══ PCI Device Discovery ════════════════════════════════════ */

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t *dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device_n = 0; device_n < 32; device_n++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor = pci_config_read_word(bus, device_n, function, PCI_VENDOR_ID);
                if (vendor == 0xFFFF) continue;

                uint16_t dev_id = pci_config_read_word(bus, device_n, function, PCI_DEVICE_ID);
                if (vendor == vendor_id && dev_id == device_id) {
                    dev->bus = bus;
                    dev->device = device_n;
                    dev->function = function;
                    dev->vendor_id = vendor;
                    dev->device_id = dev_id;
                    dev->class_code = pci_config_read_byte(bus, device_n, function, PCI_CLASS);
                    dev->subclass = pci_config_read_byte(bus, device_n, function, PCI_SUBCLASS);
                    dev->prog_if = pci_config_read_byte(bus, device_n, function, PCI_PROG_IF);
                    dev->revision = pci_config_read_byte(bus, device_n, function, PCI_REVISION_ID);
                    dev->interrupt_line = pci_config_read_byte(bus, device_n, function, PCI_INTERRUPT_LINE);
                    for (int i = 0; i < 6; i++)
                        dev->bar[i] = pci_config_read_dword(bus, device_n, function, PCI_BAR0 + i * 4);
                    return 0;
                }
            }
        }
    }
    return -1;
}

void pci_scan_bus(void) {
    serial_printf("Scanning PCI bus (ECAM at 0x%x)...\n", PCI_ECAM_BASE);
    int found = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint16_t vendor = pci_config_read_word(bus, device, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;

            uint16_t dev_id = pci_config_read_word(bus, device, 0, PCI_DEVICE_ID);
            uint8_t class_code = pci_config_read_byte(bus, device, 0, PCI_CLASS);
            uint8_t subclass = pci_config_read_byte(bus, device, 0, PCI_SUBCLASS);

            serial_printf("  PCI %d:%d.0 vendor=%x device=%x class=%x:%x\n",
                         bus, device, vendor, dev_id, class_code, subclass);
            found++;
        }
    }

    if (found == 0)
        serial_printf("  No PCI devices found\n");
    else
        serial_printf("  Found %d PCI device(s)\n", found);
}

int pci_enumerate_devices(pci_device_info_t *out, int max) {
    int count = 0;
    for (uint16_t bus = 0; bus < 256 && count < max; bus++) {
        for (uint8_t device = 0; device < 32 && count < max; device++) {
            uint16_t vendor = pci_config_read_word(bus, device, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;
            out[count].bus = (uint8_t)bus;
            out[count].device = device;
            out[count].vendor_id = vendor;
            out[count].device_id = pci_config_read_word(bus, device, 0, PCI_DEVICE_ID);
            out[count].class_code = pci_config_read_byte(bus, device, 0, PCI_CLASS);
            out[count].subclass = pci_config_read_byte(bus, device, 0, PCI_SUBCLASS);
            count++;
        }
    }
    return count;
}

void pci_initialize(void) {
    /* QEMU virt only has PCI ECAM when a PCI host bridge is configured.
     * With -device virtio-blk-device (MMIO transport), there's no PCI.
     * We detect PCI by checking if the ECAM region is readable.
     *
     * Since an unhandled external abort would crash, we rely on the
     * QEMU device tree: if PCI is needed, add -M virt,highmem=on and
     * PCI devices. For now, default to unavailable. */
    pci_available = 0;
    DBG("PCI ECAM: not available (MMIO transport in use)");
}
