#include <kernel/rtl8139.h>
#include <kernel/pci.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static rtl8139_device_t rtl8139_dev;

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

/* Simple delay function */
static void delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++);
}

/* Allocate physically contiguous memory (simplified - just use static buffers) */
static uint8_t rx_buffer_static[RTL8139_RX_BUFFER_SIZE] __attribute__((aligned(4)));
static uint8_t tx_buffer_static[RTL8139_NUM_TX_DESC][RTL8139_TX_BUFFER_SIZE] __attribute__((aligned(4)));

int rtl8139_initialize(void) {
    pci_device_t pci_dev;
    
    printf("Searching for RTL8139 network card...\n");
    
    if (pci_find_device(RTL8139_VENDOR_ID, RTL8139_DEVICE_ID, &pci_dev) != 0) {
        printf("RTL8139 not found\n");
        return -1;
    }
    
    printf("Found RTL8139 at PCI %d:%d.%d\n", 
           pci_dev.bus, pci_dev.device, pci_dev.function);
    
    /* Get I/O base address from BAR0 */
    rtl8139_dev.io_base = pci_dev.bar[0] & ~0x3;
    rtl8139_dev.irq = pci_dev.interrupt_line;
    
    printf("  I/O Base: 0x%x, IRQ: %d\n", rtl8139_dev.io_base, rtl8139_dev.irq);
    
    /* Enable PCI Bus Mastering */
    uint16_t command = pci_config_read_word(pci_dev.bus, pci_dev.device, 
                                            pci_dev.function, PCI_COMMAND);
    command |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write_word(pci_dev.bus, pci_dev.device, 
                         pci_dev.function, PCI_COMMAND, command);
    
    /* Power on the device */
    outb(rtl8139_dev.io_base + RTL8139_CONFIG1, 0x00);
    
    /* Software reset */
    printf("Resetting RTL8139...\n");
    outb(rtl8139_dev.io_base + RTL8139_CHIPCMD, RTL8139_CMD_RESET);
    
    /* Wait for reset to complete */
    int timeout = 1000;
    while ((inb(rtl8139_dev.io_base + RTL8139_CHIPCMD) & RTL8139_CMD_RESET) && timeout > 0) {
        delay_ms(1);
        timeout--;
    }
    
    if (timeout == 0) {
        printf("RTL8139 reset timeout\n");
        return -1;
    }
    
    printf("RTL8139 reset complete\n");
    
    /* Read MAC address */
    for (int i = 0; i < 6; i++) {
        rtl8139_dev.mac[i] = inb(rtl8139_dev.io_base + RTL8139_IDR0 + i);
    }
    
    printf("  MAC Address: ");
    for (int i = 0; i < 6; i++) {
        if (i > 0) putchar(':');
        int high = rtl8139_dev.mac[i] >> 4;
        int low = rtl8139_dev.mac[i] & 0xF;
        putchar(high < 10 ? '0' + high : 'a' + high - 10);
        putchar(low < 10 ? '0' + low : 'a' + low - 10);
    }
    printf("\n");
    
    /* Set up receive buffer */
    rtl8139_dev.rx_buffer = rx_buffer_static;
    rtl8139_dev.rx_buffer_phys = (uint32_t)rx_buffer_static;
    rtl8139_dev.rx_offset = 0;
    
    outl(rtl8139_dev.io_base + RTL8139_RXBUF, rtl8139_dev.rx_buffer_phys);
    
    /* Initialize CAPR (Current Address of Packet Read) - must be done BEFORE enabling RX */
    outw(rtl8139_dev.io_base + RTL8139_RXBUFTAIL, 0xFFF0);  /* -16 in 16-bit */
    
    /* Set up transmit buffers */
    for (int i = 0; i < RTL8139_NUM_TX_DESC; i++) {
        rtl8139_dev.tx_buffer[i] = tx_buffer_static[i];
        rtl8139_dev.tx_buffer_phys[i] = (uint32_t)tx_buffer_static[i];
        outl(rtl8139_dev.io_base + RTL8139_TXADDR0 + i * 4, 
             rtl8139_dev.tx_buffer_phys[i]);
    }
    rtl8139_dev.tx_current = 0;
    
    /* Enable Receive and Transmit */
    outb(rtl8139_dev.io_base + RTL8139_CHIPCMD, 
         RTL8139_CMD_RX_ENABLE | RTL8139_CMD_TX_ENABLE);
    
    /* Configure receive buffer (Accept ALL packets for debugging) */
    outl(rtl8139_dev.io_base + RTL8139_RXCONFIG, 
         RTL8139_RX_CONFIG_ACCEPT_BROADCAST | 
         RTL8139_RX_CONFIG_ACCEPT_MULTICAST | 
         RTL8139_RX_CONFIG_ACCEPT_MATCH |
         RTL8139_RX_CONFIG_ACCEPT_ALL_PHYS |  /* Promiscuous mode */
         RTL8139_RX_CONFIG_WRAP |
         RTL8139_RX_CONFIG_8K_BUFFER);  /* Try 8K buffer instead of 32K */
    
    /* Configure transmit */
    outl(rtl8139_dev.io_base + RTL8139_TXCONFIG, 
         RTL8139_TX_CONFIG_IFG96);
    
    /* Enable interrupts */
    outw(rtl8139_dev.io_base + RTL8139_INTRMASK, 
         RTL8139_INT_RX_OK | RTL8139_INT_TX_OK | 
         RTL8139_INT_RX_ERR | RTL8139_INT_TX_ERR);
    
    rtl8139_dev.initialized = 1;
    
    printf("RTL8139 initialized successfully\n");
    
    return 0;
}

int rtl8139_send_packet(const uint8_t* data, size_t len) {
    if (!rtl8139_dev.initialized) {
        return -1;
    }
    
    if (len > RTL8139_TX_BUFFER_SIZE) {
        return -1;
    }
    
    /* Copy data to transmit buffer */
    uint8_t desc = rtl8139_dev.tx_current;
    memcpy(rtl8139_dev.tx_buffer[desc], data, len);
    
    /* Send packet */
    outl(rtl8139_dev.io_base + RTL8139_TXSTATUS0 + desc * 4, len);
    
    /* Move to next descriptor */
    rtl8139_dev.tx_current = (rtl8139_dev.tx_current + 1) % RTL8139_NUM_TX_DESC;
    
    return 0;
}

int rtl8139_receive_packet(uint8_t* buffer, size_t* len) {
    if (!rtl8139_dev.initialized) {
        return -1;
    }
    
    /* Check interrupt status for received packets */
    uint16_t isr = inw(rtl8139_dev.io_base + RTL8139_INTRSTATUS);
    
    /* Acknowledge ALL interrupts */
    if (isr != 0) {
        outw(rtl8139_dev.io_base + RTL8139_INTRSTATUS, isr);
    }
    
    /* Check if there's data available */
    uint8_t cmd = inb(rtl8139_dev.io_base + RTL8139_CHIPCMD);
    if (cmd & RTL8139_CMD_BUF_EMPTY) {
        return -1;  /* No packet available */
    }
    
    /* We have data! */
    
    /* Read packet header (4 bytes: status + length) */
    uint16_t* header = (uint16_t*)(rtl8139_dev.rx_buffer + rtl8139_dev.rx_offset);
    uint16_t status = header[0];
    uint16_t packet_len = header[1];
    
    /* Check if packet is valid */
    if (!(status & 0x01)) {  /* ROK bit */
        /* Bad packet, skip it */
        rtl8139_dev.rx_offset = (rtl8139_dev.rx_offset + packet_len + 4 + 3) & ~3;
        rtl8139_dev.rx_offset = rtl8139_dev.rx_offset % 8192;
        outw(rtl8139_dev.io_base + RTL8139_RXBUFTAIL, 
             (rtl8139_dev.rx_offset - 16) & 0xFFFF);
        return -1;
    }
    
    /* Remove CRC (4 bytes) from length */
    packet_len -= 4;
    
    if (packet_len > *len) {
        packet_len = *len;
    }
    
    /* Copy packet data */
    memcpy(buffer, rtl8139_dev.rx_buffer + rtl8139_dev.rx_offset + 4, packet_len);
    *len = packet_len;
    
    /* Update read pointer (align to 4 bytes, +4 for header) */
    rtl8139_dev.rx_offset = (rtl8139_dev.rx_offset + packet_len + 4 + 4 + 3) & ~3;
    rtl8139_dev.rx_offset = rtl8139_dev.rx_offset % 8192;
    
    /* Update CAPR (Current Address of Packet Read) */
    outw(rtl8139_dev.io_base + RTL8139_RXBUFTAIL, 
         (rtl8139_dev.rx_offset - 16) & 0xFFFF);
    
    return 0;
}

void rtl8139_get_mac(uint8_t mac[6]) {
    if (rtl8139_dev.initialized) {
        memcpy(mac, rtl8139_dev.mac, 6);
    }
}

int rtl8139_is_initialized(void) {
    return rtl8139_dev.initialized;
}
