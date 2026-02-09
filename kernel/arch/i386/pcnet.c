#include <kernel/pcnet.h>
#include <kernel/pci.h>
#include <kernel/io.h>
#include <stdio.h>
#include <string.h>

static uint32_t io_base;
static uint8_t  mac_addr[6];
static int      initialized = 0;

/* Descriptor rings and buffers - statically allocated, 16-byte aligned */
static pcnet_descriptor_t rx_ring[PCNET_RX_COUNT] __attribute__((aligned(16)));
static pcnet_descriptor_t tx_ring[PCNET_TX_COUNT] __attribute__((aligned(16)));
static uint8_t rx_buffers[PCNET_RX_COUNT][PCNET_BUF_SIZE] __attribute__((aligned(16)));
static uint8_t tx_buffers[PCNET_TX_COUNT][PCNET_BUF_SIZE] __attribute__((aligned(16)));
static pcnet_init_block_t init_block __attribute__((aligned(16)));

static int rx_index = 0;
static int tx_index = 0;

/* CSR/BCR helpers (DWIO - 32-bit I/O mode) */
static void pcnet_write_csr(uint32_t reg, uint32_t val) {
    outl(io_base + PCNET_RAP, reg);
    outl(io_base + PCNET_RDP, val);
}

static uint32_t pcnet_read_csr(uint32_t reg) {
    outl(io_base + PCNET_RAP, reg);
    return inl(io_base + PCNET_RDP);
}

static void pcnet_write_bcr(uint32_t reg, uint32_t val) {
    outl(io_base + PCNET_RAP, reg);
    outl(io_base + PCNET_BDP, val);
}

static uint32_t pcnet_read_bcr(uint32_t reg) {
    outl(io_base + PCNET_RAP, reg);
    return inl(io_base + PCNET_BDP);
}

/* Simple busy-wait delay */
static void delay_ms(int ms) {
    for (volatile int i = 0; i < ms * 10000; i++);
}

/*
 * Safe CSR0 write: always preserves STRT and clears W1C bits to avoid
 * side effects. VirtualBox's PCnet emulation treats STRT as R/W rather
 * than write-1-to-set, so omitting it stops the controller.
 */
static void pcnet_csr0_write(uint32_t bits) {
    pcnet_write_csr(0, PCNET_CSR0_STRT | bits);
}

int pcnet_initialize(void) {
    pci_device_t pci_dev;

    if (pci_find_device(PCNET_VENDOR_ID, PCNET_DEVICE_ID, &pci_dev) != 0) {
        return -1;
    }

    /* Get I/O base from BAR0 */
    io_base = pci_dev.bar[0] & ~0x3;

    /* Enable PCI bus mastering + I/O space */
    uint16_t cmd = pci_config_read_word(pci_dev.bus, pci_dev.device,
                                         pci_dev.function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write_word(pci_dev.bus, pci_dev.device,
                          pci_dev.function, PCI_COMMAND, cmd);

    /* Hardware reset: 16-bit reset first, then 32-bit to switch to DWIO mode */
    inw(io_base + 0x14);
    inl(io_base + PCNET_RESET);
    delay_ms(10);

    /* Set STOP to ensure known state before configuration */
    pcnet_write_csr(0, PCNET_CSR0_STOP);

    /* Switch to 32-bit software style via BCR20, preserving upper bits */
    uint32_t bcr20 = pcnet_read_bcr(20);
    pcnet_write_bcr(20, (bcr20 & 0xFF00) | 2);
    bcr20 = pcnet_read_bcr(20);
    (void)bcr20;

    /* Read MAC address from APROM */
    for (int i = 0; i < 6; i++) {
        mac_addr[i] = inb(io_base + PCNET_APROM + i);
    }

    /*
     * Setup RX ring.
     * Write buffer address to BOTH addr (SWSTYLE 2: TMD0) and mcnt
     * (SWSTYLE 3: TMD0) so it works regardless of active style.
     */
    memset(rx_ring, 0, sizeof(rx_ring));
    for (int i = 0; i < PCNET_RX_COUNT; i++) {
        rx_ring[i].addr   = (uint32_t)rx_buffers[i];
        rx_ring[i].status = PCNET_DESC_OWN | (uint32_t)((-PCNET_BUF_SIZE) & 0x0FFF) | 0xF000;
        rx_ring[i].mcnt   = (uint32_t)rx_buffers[i];
    }

    /* Setup TX ring - same dual-address approach */
    memset(tx_ring, 0, sizeof(tx_ring));
    for (int i = 0; i < PCNET_TX_COUNT; i++) {
        tx_ring[i].addr   = (uint32_t)tx_buffers[i];
        tx_ring[i].status = 0;
        tx_ring[i].mcnt   = (uint32_t)tx_buffers[i];
    }

    /* Fill initialization block */
    memset(&init_block, 0, sizeof(init_block));
    init_block.mode = 0x0000;
    init_block.rlen = (PCNET_LOG2_RX << 4);
    init_block.tlen = (PCNET_LOG2_TX << 4);
    memcpy(init_block.padr, mac_addr, 6);
    memset(init_block.ladrf, 0xFF, 8);
    init_block.rdra = (uint32_t)rx_ring;
    init_block.tdra = (uint32_t)tx_ring;

    /* Write init block physical address to CSR1 (low 16) and CSR2 (high 16) */
    uint32_t init_addr = (uint32_t)&init_block;
    pcnet_write_csr(1, init_addr & 0xFFFF);
    pcnet_write_csr(2, (init_addr >> 16) & 0xFFFF);

    /* Trigger initialization: set INIT bit in CSR0 */
    pcnet_write_csr(0, PCNET_CSR0_INIT);

    /* Poll for IDON (initialization done) */
    int timeout = 1000;
    while (!(pcnet_read_csr(0) & PCNET_CSR0_IDON) && timeout > 0) {
        delay_ms(1);
        timeout--;
    }

    if (timeout == 0) {
        printf("PCnet: initialization timeout\n");
        return -1;
    }

    /* Clear IDON and start the controller - NO IENA (polling only) */
    pcnet_write_csr(0, PCNET_CSR0_IDON | PCNET_CSR0_STRT);

    /* Verify controller is running */
    uint32_t csr0 = pcnet_read_csr(0);
    if (!(csr0 & PCNET_CSR0_TXON) || !(csr0 & PCNET_CSR0_RXON)) {
        printf("PCnet: failed to start\n");
        return -1;
    }

    rx_index = 0;
    tx_index = 0;
    initialized = 1;

    return 0;
}

int pcnet_send_packet(const uint8_t* data, size_t len) {
    if (!initialized) return -1;
    if (len > PCNET_BUF_SIZE) return -1;

    int cur = tx_index;

    /* Wait until descriptor is owned by CPU */
    if (tx_ring[cur].status & PCNET_DESC_OWN) {
        return -1;
    }

    /* Copy data into TX buffer */
    memcpy(tx_buffers[cur], data, len);

    /* Set buffer address in BOTH fields (SWSTYLE 2 uses addr, 3 uses mcnt) */
    tx_ring[cur].addr = (uint32_t)tx_buffers[cur];
    tx_ring[cur].mcnt = (uint32_t)tx_buffers[cur];

    /* Set descriptor: OWN | STP | ENP, BCNT = -len (12-bit two's complement) */
    tx_ring[cur].status = PCNET_DESC_OWN | PCNET_DESC_STP | PCNET_DESC_ENP |
                          (uint32_t)((-len) & 0x0FFF) | 0xF000;

    /* Memory barrier to ensure descriptor is written before TDMD */
    __asm__ __volatile__("" ::: "memory");

    /* Trigger transmit demand - MUST include STRT (VirtualBox quirk) */
    pcnet_csr0_write(PCNET_CSR0_TDMD);

    /* Poll for TX completion (up to 200ms) */
    for (int i = 0; i < 200; i++) {
        if (!(tx_ring[cur].status & PCNET_DESC_OWN)) {
            pcnet_csr0_write(PCNET_CSR0_TINT);
            break;
        }
        delay_ms(1);
    }

    tx_index = (tx_index + 1) % PCNET_TX_COUNT;
    return 0;
}

int pcnet_receive_packet(uint8_t* buffer, size_t* len) {
    if (!initialized) return -1;

    /* Check if current RX descriptor is owned by CPU (OWN=0) */
    if (rx_ring[rx_index].status & PCNET_DESC_OWN) {
        return -1;
    }

    /* Check for errors */
    if (rx_ring[rx_index].status & PCNET_DESC_ERR) {
        goto recycle;
    }

    {
        /*
         * Read message byte count: try SWSTYLE 2 (mcnt field) first,
         * fall back to SWSTYLE 3 (addr field).
         */
        uint32_t pkt_len = (rx_ring[rx_index].mcnt & 0x0FFF);
        if (pkt_len == 0 || pkt_len > PCNET_BUF_SIZE) {
            pkt_len = (rx_ring[rx_index].addr & 0x0FFF);
        }

        if (pkt_len >= 4) {
            pkt_len -= 4; /* Remove CRC */
        }

        if (pkt_len == 0 || pkt_len > *len) {
            if (pkt_len == 0) goto recycle;
            pkt_len = *len;
        }

        memcpy(buffer, rx_buffers[rx_index], pkt_len);
        *len = pkt_len;
    }

    pcnet_csr0_write(PCNET_CSR0_RINT);

recycle:
    /* Give descriptor back - set buffer address in BOTH fields */
    rx_ring[rx_index].addr   = (uint32_t)rx_buffers[rx_index];
    rx_ring[rx_index].mcnt   = (uint32_t)rx_buffers[rx_index];
    rx_ring[rx_index].status = PCNET_DESC_OWN | (uint32_t)((-PCNET_BUF_SIZE) & 0x0FFF) | 0xF000;
    rx_index = (rx_index + 1) % PCNET_RX_COUNT;

    return 0;
}

void pcnet_get_mac(uint8_t mac[6]) {
    if (initialized) {
        memcpy(mac, mac_addr, 6);
    }
}

int pcnet_is_initialized(void) {
    return initialized;
}
