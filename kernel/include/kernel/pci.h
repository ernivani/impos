#ifndef _KERNEL_PCI_H
#define _KERNEL_PCI_H

#include <stdint.h>

/* PCI Configuration Space Registers */
#define PCI_VENDOR_ID           0x00
#define PCI_DEVICE_ID           0x02
#define PCI_COMMAND             0x04
#define PCI_STATUS              0x06
#define PCI_REVISION_ID         0x08
#define PCI_PROG_IF             0x09
#define PCI_SUBCLASS            0x0A
#define PCI_CLASS               0x0B
#define PCI_CACHE_LINE_SIZE     0x0C
#define PCI_LATENCY_TIMER       0x0D
#define PCI_HEADER_TYPE         0x0E
#define PCI_BIST                0x0F
#define PCI_BAR0                0x10
#define PCI_BAR1                0x14
#define PCI_BAR2                0x18
#define PCI_BAR3                0x1C
#define PCI_BAR4                0x20
#define PCI_BAR5                0x24
#define PCI_INTERRUPT_LINE      0x3C
#define PCI_INTERRUPT_PIN       0x3D

/* PCI Command Register Bits */
#define PCI_COMMAND_IO          0x01
#define PCI_COMMAND_MEMORY      0x02
#define PCI_COMMAND_MASTER      0x04
#define PCI_COMMAND_SPECIAL     0x08
#define PCI_COMMAND_INVALIDATE  0x10
#define PCI_COMMAND_VGA_PALETTE 0x20
#define PCI_COMMAND_PARITY      0x40
#define PCI_COMMAND_WAIT        0x80
#define PCI_COMMAND_SERR        0x100
#define PCI_COMMAND_FAST_BACK   0x200
#define PCI_COMMAND_INTX_DISABLE 0x400

/* PCI Device Structure */
typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t interrupt_line;
    uint32_t bar[6];
} pci_device_t;

/* PCI Functions */
void pci_initialize(void);
uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);
void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value);
void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value);

/* Find PCI device by vendor/device ID */
int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* dev);

/* Scan all PCI devices */
void pci_scan_bus(void);

/* PCI enumeration for GUI apps */
typedef struct {
    uint8_t  bus;
    uint8_t  device;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t  class_code;
    uint8_t  subclass;
} pci_device_info_t;

int pci_enumerate_devices(pci_device_info_t *out, int max);

#endif
