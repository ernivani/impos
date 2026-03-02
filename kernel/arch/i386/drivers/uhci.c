#include <kernel/uhci.h>
#include <kernel/pci.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* ── Static State ────────────────────────────────────────────────── */

static uint16_t io_base = 0;
static int uhci_present = 0;

/* Frame list: 1024 entries, 4KB aligned */
static uint32_t frame_list[1024] __attribute__((aligned(4096)));

/* TD/QH pool (statically allocated for simplicity) */
#define TD_POOL_SIZE 16
#define QH_POOL_SIZE 4
static uhci_td_t td_pool[TD_POOL_SIZE] __attribute__((aligned(16)));
static uhci_qh_t qh_pool[QH_POOL_SIZE] __attribute__((aligned(16)));

/* Data buffers for control transfers */
static uint8_t setup_buf[8] __attribute__((aligned(16)));
static uint8_t data_buf[64] __attribute__((aligned(16)));

/* Discovered devices */
#define MAX_USB_DEVICES 8
static usb_device_desc_t devices[MAX_USB_DEVICES];
static int device_count = 0;

/* ── I/O Helpers ─────────────────────────────────────────────────── */

static inline uint16_t uhci_read16(uint16_t reg) {
    return inw(io_base + reg);
}

static inline void uhci_write16(uint16_t reg, uint16_t val) {
    outw(io_base + reg, val);
}

static inline uint32_t uhci_read32(uint16_t reg) {
    return inl(io_base + reg);
}

static inline void uhci_write32(uint16_t reg, uint32_t val) {
    outl(io_base + reg, val);
}

/* ── Delay ───────────────────────────────────────────────────────── */

extern void pit_sleep_ms(uint32_t ms);

/* ── Port Operations ─────────────────────────────────────────────── */

static int uhci_port_reset(uint16_t port_reg) {
    /* Check if device is connected */
    uint16_t status = uhci_read16(port_reg);
    if (!(status & UHCI_PORT_CCS))
        return -1;  /* No device */

    /* Set port reset */
    uhci_write16(port_reg, UHCI_PORT_RESET);
    pit_sleep_ms(50);

    /* Clear reset */
    uhci_write16(port_reg, 0);
    pit_sleep_ms(10);

    /* Enable port */
    for (int i = 0; i < 10; i++) {
        status = uhci_read16(port_reg);
        if (status & UHCI_PORT_CCS) {
            /* Clear status change bits by writing 1 to them */
            uhci_write16(port_reg, UHCI_PORT_PE | UHCI_PORT_CSC | UHCI_PORT_PEC);
            pit_sleep_ms(10);
            status = uhci_read16(port_reg);
            if (status & UHCI_PORT_PE)
                return 0;  /* Port enabled */
        }
        pit_sleep_ms(10);
    }

    return -1;
}

/* ── Control Transfer ────────────────────────────────────────────── */

static int uhci_control_transfer(uint8_t dev_addr, uint8_t *setup_data,
                                  uint8_t *recv_buf, uint16_t recv_len) {
    /* Build SETUP TD */
    uhci_td_t *td_setup = &td_pool[0];
    uhci_td_t *td_data  = &td_pool[1];
    uhci_td_t *td_status = &td_pool[2];
    uhci_qh_t *qh = &qh_pool[0];

    memset(td_setup, 0, sizeof(*td_setup));
    memset(td_data, 0, sizeof(*td_data));
    memset(td_status, 0, sizeof(*td_status));
    memset(qh, 0, sizeof(*qh));

    /* Copy setup data */
    memcpy(setup_buf, setup_data, 8);

    /* SETUP TD: PID=SETUP, addr=dev_addr, endpt=0, toggle=0, maxlen=7 (8 bytes) */
    td_setup->link = (uint32_t)td_data | UHCI_LP_DEPTH;
    td_setup->status = UHCI_TD_ACTIVE | (3 << 27);  /* 3 error retries */
    td_setup->token = UHCI_PID_SETUP |
                      ((uint32_t)dev_addr << 8) |
                      (0 << 15) |                   /* endpoint 0 */
                      (0 << 19) |                   /* data toggle 0 */
                      ((uint32_t)(8 - 1) << 21);    /* max length = 7 (8 bytes) */
    td_setup->buffer = (uint32_t)setup_buf;

    /* DATA IN TD: PID=IN, toggle=1 */
    if (recv_len > 0) {
        memset(data_buf, 0, sizeof(data_buf));
        td_data->link = (uint32_t)td_status | UHCI_LP_DEPTH;
        td_data->status = UHCI_TD_ACTIVE | (3 << 27);
        td_data->token = UHCI_PID_IN |
                         ((uint32_t)dev_addr << 8) |
                         (0 << 15) |
                         (1 << 19) |                /* data toggle 1 */
                         ((uint32_t)(recv_len - 1) << 21);
        td_data->buffer = (uint32_t)data_buf;
    } else {
        td_setup->link = (uint32_t)td_status | UHCI_LP_DEPTH;
    }

    /* STATUS OUT TD: PID=OUT, zero-length, toggle=1 */
    td_status->link = UHCI_LP_TERMINATE;
    td_status->status = UHCI_TD_ACTIVE | (3 << 27);
    td_status->token = UHCI_PID_OUT |
                       ((uint32_t)dev_addr << 8) |
                       (0 << 15) |
                       (1 << 19) |
                       (0x7FF << 21);   /* max length = 0x7FF means zero-length */
    td_status->buffer = 0;

    /* Set up QH pointing to first TD */
    qh->head = UHCI_LP_TERMINATE;
    qh->element = (uint32_t)td_setup;

    /* Insert QH into frame list entry 0 */
    uint32_t old_fl0 = frame_list[0];
    frame_list[0] = (uint32_t)qh | UHCI_LP_QH;

    /* Wait for completion (poll) */
    int timeout = 500;  /* ~500ms */
    int success = 0;

    while (timeout > 0) {
        pit_sleep_ms(1);
        timeout--;

        /* Check if all TDs are done */
        int all_done = 1;
        if (td_setup->status & UHCI_TD_ACTIVE) all_done = 0;
        if (recv_len > 0 && (td_data->status & UHCI_TD_ACTIVE)) all_done = 0;
        if (td_status->status & UHCI_TD_ACTIVE) all_done = 0;

        if (all_done) {
            /* Check for errors */
            uint32_t err_mask = UHCI_TD_STALLED | UHCI_TD_DATABUF |
                                UHCI_TD_BABBLE | UHCI_TD_CRCTMO | UHCI_TD_BITSTUFF;
            if (!(td_setup->status & err_mask) &&
                !(td_status->status & err_mask) &&
                (recv_len == 0 || !(td_data->status & err_mask))) {
                success = 1;
            }
            break;
        }
    }

    /* Restore frame list */
    frame_list[0] = old_fl0;

    if (success && recv_len > 0) {
        memcpy(recv_buf, data_buf, recv_len);
    }

    return success ? 0 : -1;
}

/* ── GET_DESCRIPTOR ──────────────────────────────────────────────── */

static int uhci_get_device_descriptor(uint8_t dev_addr, usb_device_desc_t *desc) {
    uint8_t setup[8] = {
        0x80,                      /* bmRequestType: device-to-host, standard, device */
        USB_REQ_GET_DESCRIPTOR,    /* bRequest */
        0x00, USB_DESC_DEVICE,     /* wValue: descriptor type + index */
        0x00, 0x00,                /* wIndex */
        18, 0                      /* wLength: 18 bytes */
    };

    return uhci_control_transfer(dev_addr, setup, (uint8_t *)desc, 18);
}

/* ── Enumerate Port ──────────────────────────────────────────────── */

static void uhci_enumerate_port(uint16_t port_reg, int port_num) {
    if (uhci_port_reset(port_reg) < 0)
        return;

    if (device_count >= MAX_USB_DEVICES)
        return;

    /* Device is at address 0 after reset — get descriptor */
    usb_device_desc_t desc;
    memset(&desc, 0, sizeof(desc));

    /* First try: get 8 bytes to learn bMaxPacketSize0 */
    if (uhci_get_device_descriptor(0, &desc) < 0) {
        DBG("[UHCI] Port %d: failed to get device descriptor", port_num);
        return;
    }

    if (desc.bDescriptorType != USB_DESC_DEVICE) {
        DBG("[UHCI] Port %d: invalid descriptor type %d", port_num, desc.bDescriptorType);
        return;
    }

    memcpy(&devices[device_count], &desc, sizeof(desc));
    device_count++;

    DBG("[UHCI] Port %d: USB %x.%x device %04x:%04x class=%02x/%02x",
        port_num, desc.bcdUSB >> 8, desc.bcdUSB & 0xFF,
        desc.idVendor, desc.idProduct,
        desc.bDeviceClass, desc.bDeviceSubClass);
}

/* ── Initialization ──────────────────────────────────────────────── */

void uhci_initialize(void) {
    /* Find UHCI controller on PCI bus.
     * UHCI is PCI class 0x0C (Serial Bus), subclass 0x03 (USB),
     * prog-if 0x00 (UHCI). Multiple vendors implement it. */
    pci_device_t dev;
    int found = 0;

    /* Try common UHCI PCI IDs */
    /* Intel PIIX3/PIIX4 UHCI */
    if (pci_find_device(0x8086, 0x7020, &dev) == 0) found = 1;
    if (!found && pci_find_device(0x8086, 0x7112, &dev) == 0) found = 1;
    /* QEMU ICH9 UHCI */
    if (!found && pci_find_device(0x8086, 0x2934, &dev) == 0) found = 1;
    if (!found && pci_find_device(0x8086, 0x2935, &dev) == 0) found = 1;
    if (!found && pci_find_device(0x8086, 0x2936, &dev) == 0) found = 1;

    /* Fallback: scan for any USB UHCI controller by class */
    if (!found) {
        for (int bus = 0; bus < 256 && !found; bus++) {
            for (int slot = 0; slot < 32 && !found; slot++) {
                uint32_t id = pci_config_read_dword(bus, slot, 0, 0);
                if (id == 0xFFFFFFFF) continue;

                uint8_t cls = pci_config_read_byte(bus, slot, 0, PCI_CLASS);
                uint8_t sub = pci_config_read_byte(bus, slot, 0, PCI_SUBCLASS);
                uint8_t pif = pci_config_read_byte(bus, slot, 0, PCI_PROG_IF);

                if (cls == 0x0C && sub == 0x03 && pif == 0x00) {
                    dev.bus = bus;
                    dev.device = slot;
                    dev.function = 0;
                    dev.vendor_id = id & 0xFFFF;
                    dev.device_id = (id >> 16) & 0xFFFF;
                    dev.bar[4] = pci_config_read_dword(bus, slot, 0, PCI_BAR4);
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        DBG("[UHCI] No UHCI controller found");
        return;
    }

    /* BAR4 contains the I/O base address */
    io_base = (uint16_t)(dev.bar[4] & ~0x03);
    if (io_base == 0) {
        io_base = (uint16_t)(pci_config_read_dword(dev.bus, dev.device,
                                                    dev.function, PCI_BAR4) & ~0x03);
    }
    if (io_base == 0) {
        DBG("[UHCI] Invalid I/O base");
        return;
    }

    DBG("[UHCI] Found controller %04x:%04x at I/O 0x%x",
        dev.vendor_id, dev.device_id, io_base);

    /* Enable bus mastering */
    uint16_t cmd = pci_config_read_word(dev.bus, dev.device, dev.function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write_word(dev.bus, dev.device, dev.function, PCI_COMMAND, cmd);

    /* Global reset */
    uhci_write16(UHCI_USBCMD, UHCI_CMD_GRESET);
    pit_sleep_ms(50);
    uhci_write16(UHCI_USBCMD, 0);
    pit_sleep_ms(10);

    /* Host controller reset */
    uhci_write16(UHCI_USBCMD, UHCI_CMD_HCRESET);
    for (int i = 0; i < 100; i++) {
        if (!(uhci_read16(UHCI_USBCMD) & UHCI_CMD_HCRESET))
            break;
        pit_sleep_ms(1);
    }

    /* Initialize frame list — all entries point to terminate */
    for (int i = 0; i < 1024; i++)
        frame_list[i] = UHCI_LP_TERMINATE;

    /* Set frame list base address */
    uhci_write32(UHCI_FLBASEADD, (uint32_t)frame_list);

    /* Set frame number to 0 */
    uhci_write16(UHCI_FRNUM, 0);

    /* Set SOF timing */
    outb(io_base + UHCI_SOFMOD, 64);

    /* Clear status */
    uhci_write16(UHCI_USBSTS, 0xFFFF);

    /* Start controller */
    uhci_write16(UHCI_USBCMD, UHCI_CMD_RS | UHCI_CMD_MAXP);
    pit_sleep_ms(10);

    /* Verify controller is running */
    if (uhci_read16(UHCI_USBSTS) & UHCI_STS_HCH) {
        DBG("[UHCI] Controller failed to start");
        return;
    }

    uhci_present = 1;
    DBG("[UHCI] Controller started");

    /* Enumerate ports */
    uhci_enumerate_port(UHCI_PORTSC1, 1);
    uhci_enumerate_port(UHCI_PORTSC2, 2);

    DBG("[UHCI] Enumeration complete: %d device(s)", device_count);
}

/* ── Public API ──────────────────────────────────────────────────── */

int uhci_get_device_count(void) {
    return device_count;
}

int uhci_get_device_info(int idx, uint16_t *vendor, uint16_t *product,
                         uint8_t *dev_class, uint8_t *dev_subclass) {
    if (idx < 0 || idx >= device_count) return -1;

    *vendor    = devices[idx].idVendor;
    *product   = devices[idx].idProduct;
    *dev_class = devices[idx].bDeviceClass;
    *dev_subclass = devices[idx].bDeviceSubClass;
    return 0;
}
