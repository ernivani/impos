#include <kernel/pci.h>
#include <stdio.h>
#include <string.h>

/* PCI Configuration Space Access */
#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC

/* I/O port functions */
static inline void outl(uint16_t port, uint32_t value) {
    __asm__ volatile ("outl %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t value;
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outw(uint16_t port, uint16_t value) {
    __asm__ volatile ("outw %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t value;
    __asm__ volatile ("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

static inline void outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile ("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

uint32_t pci_config_read_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | 
                       (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    return inl(PCI_CONFIG_DATA);
}

uint16_t pci_config_read_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_config_read_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_config_write_dword(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
    uint32_t address = (uint32_t)((bus << 16) | (device << 11) | 
                       (function << 8) | (offset & 0xFC) | 0x80000000);
    outl(PCI_CONFIG_ADDRESS, address);
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write_word(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint16_t value) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    dword &= ~(0xFFFF << ((offset & 2) * 8));
    dword |= (value << ((offset & 2) * 8));
    pci_config_write_dword(bus, device, function, offset & 0xFC, dword);
}

void pci_config_write_byte(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint8_t value) {
    uint32_t dword = pci_config_read_dword(bus, device, function, offset & 0xFC);
    dword &= ~(0xFF << ((offset & 3) * 8));
    dword |= (value << ((offset & 3) * 8));
    pci_config_write_dword(bus, device, function, offset & 0xFC, dword);
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id, pci_device_t* dev) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            for (uint8_t function = 0; function < 8; function++) {
                uint16_t vendor = pci_config_read_word(bus, device, function, PCI_VENDOR_ID);
                if (vendor == 0xFFFF) continue;
                
                uint16_t dev_id = pci_config_read_word(bus, device, function, PCI_DEVICE_ID);
                
                if (vendor == vendor_id && dev_id == device_id) {
                    dev->bus = bus;
                    dev->device = device;
                    dev->function = function;
                    dev->vendor_id = vendor;
                    dev->device_id = dev_id;
                    dev->class_code = pci_config_read_byte(bus, device, function, PCI_CLASS);
                    dev->subclass = pci_config_read_byte(bus, device, function, PCI_SUBCLASS);
                    dev->prog_if = pci_config_read_byte(bus, device, function, PCI_PROG_IF);
                    dev->revision = pci_config_read_byte(bus, device, function, PCI_REVISION_ID);
                    dev->interrupt_line = pci_config_read_byte(bus, device, function, PCI_INTERRUPT_LINE);
                    
                    for (int i = 0; i < 6; i++) {
                        dev->bar[i] = pci_config_read_dword(bus, device, function, PCI_BAR0 + i * 4);
                    }
                    
                    return 0;
                }
            }
        }
    }
    return -1;
}

void pci_scan_bus(void) {
    printf("Scanning PCI bus...\n");
    int found = 0;
    
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t device = 0; device < 32; device++) {
            uint16_t vendor = pci_config_read_word(bus, device, 0, PCI_VENDOR_ID);
            if (vendor == 0xFFFF) continue;
            
            uint16_t dev_id = pci_config_read_word(bus, device, 0, PCI_DEVICE_ID);
            uint8_t class_code = pci_config_read_byte(bus, device, 0, PCI_CLASS);
            uint8_t subclass = pci_config_read_byte(bus, device, 0, PCI_SUBCLASS);
            
            printf("PCI %d:%d.0 - Vendor: ", bus, device);
            
            /* Print vendor ID in hex with leading zeros */
            if (vendor < 0x1000) putchar('0');
            if (vendor < 0x100) putchar('0');
            if (vendor < 0x10) putchar('0');
            printf("%x", vendor);
            
            printf(" Device: ");
            
            /* Print device ID in hex with leading zeros */
            if (dev_id < 0x1000) putchar('0');
            if (dev_id < 0x100) putchar('0');
            if (dev_id < 0x10) putchar('0');
            printf("%x", dev_id);
            
            printf(" Class: ");
            
            /* Print class code in hex with leading zeros */
            if (class_code < 0x10) putchar('0');
            printf("%x", class_code);
            
            putchar(':');
            
            /* Print subclass in hex with leading zeros */
            if (subclass < 0x10) putchar('0');
            printf("%x", subclass);
            
            printf("\n");
            
            found++;
        }
    }
    
    if (found == 0) {
        printf("No PCI devices found\n");
    } else {
        printf("Found %d PCI device(s)\n", found);
    }
}

void pci_initialize(void) {
    /* PCI is initialized on-demand */
}
