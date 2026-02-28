/* virtio_input.c — VirtIO tablet driver for absolute mouse input
 *
 * Detects a virtio-tablet-pci device and polls its eventq for
 * EV_ABS / EV_KEY events, injecting them into the PS/2 mouse layer.
 * This bypasses display-backend mouse grab issues on WSL2.
 */

#include <kernel/virtio_input.h>
#include <kernel/pci.h>
#include <kernel/io.h>
#include <kernel/idt.h>
#include <kernel/mouse.h>
#include <kernel/gfx.h>
#include <string.h>

/* ═══ Linux evdev event types / codes ══════════════════════════ */

#define EV_SYN       0x00
#define EV_KEY       0x01
#define EV_REL       0x02
#define EV_ABS       0x03

#define SYN_REPORT   0x00
#define ABS_X        0x00
#define ABS_Y        0x01

#define BTN_LEFT     0x110
#define BTN_RIGHT    0x111
#define BTN_MIDDLE   0x112

struct virtio_input_event {
    uint16_t type;
    uint16_t code;
    uint32_t value;
} __attribute__((packed));

/* ═══ VirtIO legacy I/O registers ══════════════════════════════ */

#define VIRTIO_VENDOR_ID  0x1AF4

#define VIO_FEATURES      0x00
#define VIO_DRV_FEATURES  0x04
#define VIO_QUEUE_PFN     0x08
#define VIO_QUEUE_SIZE    0x0C  /* 16-bit */
#define VIO_QUEUE_SEL     0x0E  /* 16-bit */
#define VIO_QUEUE_NOTIFY  0x10  /* 16-bit */
#define VIO_STATUS        0x12  /* 8-bit  */
#define VIO_ISR           0x13  /* 8-bit  */

#define VIRTIO_STATUS_ACK        0x01
#define VIRTIO_STATUS_DRIVER     0x02
#define VIRTIO_STATUS_DRIVER_OK  0x04

/* Descriptor flags */
#define VRING_DESC_F_WRITE  0x02

/* ═══ Virtqueue (simplified — reused layout from virtio_gpu) ═══ */

struct vring_desc_vi {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail_vi {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem_vi {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used_vi {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem_vi ring[];
} __attribute__((packed));

/* ═══ Modern MMIO structures ═══════════════════════════════════ */

#define VIRTIO_PCI_CAP_COMMON_CFG  1
#define VIRTIO_PCI_CAP_NOTIFY_CFG  2
#define VIRTIO_PCI_CAP_ISR_CFG     3
#define VIRTIO_PCI_CAP_DEVICE_CFG  4
#define PCI_CAP_PTR_VI             0x34
#define PCI_CAP_ID_VNDR_VI         0x09

struct virtio_pci_common_cfg_vi {
    uint32_t device_feature_select;
    uint32_t device_feature;
    uint32_t driver_feature_select;
    uint32_t driver_feature;
    uint16_t msix_config;
    uint16_t num_queues;
    uint8_t  device_status;
    uint8_t  config_generation;
    uint16_t queue_select;
    uint16_t queue_size;
    uint16_t queue_msix_vector;
    uint16_t queue_enable;
    uint16_t queue_notify_off;
    uint32_t queue_desc_lo;
    uint32_t queue_desc_hi;
    uint32_t queue_driver_lo;
    uint32_t queue_driver_hi;
    uint32_t queue_device_lo;
    uint32_t queue_device_hi;
} __attribute__((packed));

/* ═══ Driver state ═════════════════════════════════════════════ */

static int vi_active = 0;
static int vi_modern = 0;
static uint16_t vi_iobase;

/* Modern MMIO pointers */
static volatile struct virtio_pci_common_cfg_vi *vi_common;
static volatile uint8_t *vi_notify_base;
static uint32_t vi_notify_mult;
static volatile uint16_t *vi_eventq_notify;

/* Eventq memory — page-aligned */
#define VI_QUEUE_SIZE 64
static uint8_t vi_eventq_mem[16384] __attribute__((aligned(4096)));

static struct vring_desc_vi  *vi_desc;
static struct vring_avail_vi *vi_avail;
static struct vring_used_vi  *vi_used;
static uint16_t vi_last_used_idx;

/* Pre-allocated event receive buffers */
static struct virtio_input_event vi_events[VI_QUEUE_SIZE]
    __attribute__((aligned(64)));

/* Accumulated state for current event group */
static int vi_pending_x = -1;
static int vi_pending_y = -1;
static uint8_t vi_btn_state = 0;

/* Axis range (QEMU virtio-tablet uses 0..32767) */
#define VI_ABS_MAX 32767

/* ═══ Virtqueue helpers ════════════════════════════════════════ */

static void vi_vq_init(void) {
    memset(vi_eventq_mem, 0, sizeof(vi_eventq_mem));

    vi_desc  = (struct vring_desc_vi *)vi_eventq_mem;
    vi_avail = (struct vring_avail_vi *)(vi_eventq_mem + VI_QUEUE_SIZE * 16);
    uint32_t avail_end = VI_QUEUE_SIZE * 16 + 4 + VI_QUEUE_SIZE * 2 + 2;
    uint32_t used_off  = (avail_end + 4095) & ~4095u;
    vi_used  = (struct vring_used_vi *)(vi_eventq_mem + used_off);

    vi_avail->flags = 1;  /* VRING_AVAIL_F_NO_INTERRUPT */
    vi_last_used_idx = 0;

    /* Pre-post all buffers: each descriptor points to an event buffer,
       marked WRITE so the device can fill it. */
    for (uint16_t i = 0; i < VI_QUEUE_SIZE; i++) {
        vi_desc[i].addr  = (uint32_t)&vi_events[i];
        vi_desc[i].len   = sizeof(struct virtio_input_event);
        vi_desc[i].flags = VRING_DESC_F_WRITE;
        vi_desc[i].next  = 0;

        vi_avail->ring[i] = i;
    }
    vi_avail->idx = VI_QUEUE_SIZE;
}

static void vi_notify(void) {
    if (vi_modern) {
        *vi_eventq_notify = 0;
    } else {
        outw(vi_iobase + VIO_QUEUE_NOTIFY, 0);
    }
}

/* Re-post a single descriptor back to the available ring */
static void vi_repost(uint16_t desc_idx) {
    uint16_t avail_idx = vi_avail->idx;
    vi_avail->ring[avail_idx % VI_QUEUE_SIZE] = desc_idx;
    __asm__ volatile("" ::: "memory");
    vi_avail->idx = avail_idx + 1;
    vi_notify();
}

/* ═══ Modern MMIO capability parsing ═══════════════════════════ */

static int vi_parse_caps(pci_device_t *dev) {
    vi_common = NULL;
    vi_notify_base = NULL;
    vi_notify_mult = 0;

    uint16_t status = pci_config_read_word(dev->bus, dev->device,
                                            dev->function, PCI_STATUS);
    if (!(status & (1 << 4))) return 0;

    uint8_t cap_ptr = pci_config_read_byte(dev->bus, dev->device,
                                            dev->function, PCI_CAP_PTR_VI);
    cap_ptr &= 0xFC;

    while (cap_ptr) {
        uint8_t cap_id = pci_config_read_byte(dev->bus, dev->device,
                                               dev->function, cap_ptr);
        uint8_t cap_next = pci_config_read_byte(dev->bus, dev->device,
                                                 dev->function, cap_ptr + 1);

        if (cap_id == PCI_CAP_ID_VNDR_VI) {
            uint8_t cfg_type = pci_config_read_byte(dev->bus, dev->device,
                                                     dev->function, cap_ptr + 3);
            uint8_t bar_idx = pci_config_read_byte(dev->bus, dev->device,
                                                    dev->function, cap_ptr + 4);
            uint32_t offset = pci_config_read_dword(dev->bus, dev->device,
                                                     dev->function, cap_ptr + 8);
            uint32_t base = (bar_idx < 6) ? (dev->bar[bar_idx] & ~0xFu) : 0;

            if (base) {
                switch (cfg_type) {
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    vi_common = (volatile struct virtio_pci_common_cfg_vi *)
                                (base + offset);
                    break;
                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    vi_notify_base = (volatile uint8_t *)(base + offset);
                    vi_notify_mult = pci_config_read_dword(
                        dev->bus, dev->device, dev->function, cap_ptr + 16);
                    break;
                }
            }
        }
        cap_ptr = cap_next;
    }

    return vi_common != NULL && vi_notify_base != NULL;
}

/* ═══ Initialization ═══════════════════════════════════════════ */

int virtio_input_init(void) {
    pci_device_t dev;
    int found = 0;

    DBG("[virtio-input] Scanning for VirtIO input devices...");

    /* Single PCI bus scan: look for any VirtIO device (vendor 0x1AF4)
       that is an input device (device 0x1052 modern, or transitional
       with subsystem device ID 18). */
    for (int bus = 0; bus < 256 && !found; bus++) {
        for (int slot = 0; slot < 32 && !found; slot++) {
            uint32_t reg0 = pci_config_read_dword((uint8_t)bus,
                                                   (uint8_t)slot, 0, 0);
            uint16_t vid = (uint16_t)(reg0 & 0xFFFF);
            uint16_t did = (uint16_t)(reg0 >> 16);
            if (vid != VIRTIO_VENDOR_ID) continue;

            uint16_t subsys = pci_config_read_word((uint8_t)bus,
                                                    (uint8_t)slot, 0, 0x2E);
            DBG("[virtio-input]   PCI %d:%d.0 vid=%04x did=%04x subsys=%u",
                bus, slot, vid, did, subsys);

            int is_input = 0;
            if (did == 0x1052) is_input = 1;          /* Modern */
            else if (subsys == 18) is_input = 1;      /* Transitional */

            if (is_input) {
                /* Fill pci_device_t struct */
                if (pci_find_device(vid, did, &dev) == 0) {
                    found = 1;
                }
            }
        }
    }

    if (!found) {
        DBG("[virtio-input] No VirtIO input device found");
        return 0;
    }

    DBG("[virtio-input] Using PCI %d:%d.%d did=%04x BAR0=0x%x BAR1=0x%x BAR4=0x%x",
        dev.bus, dev.device, dev.function, dev.device_id,
        dev.bar[0], dev.bar[1], dev.bar[4]);

    /* Enable PCI bus mastering + memory + disable INTx */
    uint16_t cmd = pci_config_read_word(dev.bus, dev.device,
                                         dev.function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER
         | PCI_COMMAND_INTX_DISABLE;
    pci_config_write_word(dev.bus, dev.device, dev.function,
                          PCI_COMMAND, cmd);

    /* Check for I/O BAR (legacy) */
    vi_iobase = 0;
    for (int i = 0; i < 6; i++) {
        if (dev.bar[i] & 0x1) {
            vi_iobase = (uint16_t)(dev.bar[i] & ~0x3u);
            break;
        }
    }

    /* Try modern MMIO first */
    if (vi_parse_caps(&dev)) {
        vi_modern = 1;
        DBG("[virtio-input] Using modern MMIO");
    } else if (vi_iobase) {
        vi_modern = 0;
        DBG("[virtio-input] Using legacy I/O at 0x%x", vi_iobase);
    } else {
        DBG("[virtio-input] No usable BAR");
        return 0;
    }

    /* ── Device init sequence ─────────────────────────────────── */

    if (vi_modern) {
        vi_common->device_status = 0;
        __asm__ volatile("" ::: "memory");

        vi_common->device_status = VIRTIO_STATUS_ACK;
        vi_common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER;

        /* No features needed */
        vi_common->device_feature_select = 0;
        (void)vi_common->device_feature;
        vi_common->driver_feature_select = 0;
        vi_common->driver_feature = 0;

        vi_common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER
                                 | 0x08; /* FEATURES_OK */
        __asm__ volatile("" ::: "memory");

        if (!(vi_common->device_status & 0x08)) {
            DBG("[virtio-input] FEATURES_OK rejected");
            return 0;
        }

        /* Disable MSI-X */
        vi_common->msix_config = 0xFFFF;

        /* Setup eventq (queue 0) */
        vi_common->queue_select = 0;
        uint16_t qsz = vi_common->queue_size;
        if (qsz == 0 || qsz > VI_QUEUE_SIZE) qsz = VI_QUEUE_SIZE;
        vi_common->queue_size = qsz;

        vi_vq_init();

        vi_common->queue_desc_lo   = (uint32_t)vi_desc;
        vi_common->queue_desc_hi   = 0;
        vi_common->queue_driver_lo = (uint32_t)vi_avail;
        vi_common->queue_driver_hi = 0;
        vi_common->queue_device_lo = (uint32_t)vi_used;
        vi_common->queue_device_hi = 0;
        vi_common->queue_msix_vector = 0xFFFF;
        vi_common->queue_enable    = 1;

        uint16_t noff = vi_common->queue_notify_off;
        vi_eventq_notify = (volatile uint16_t *)
            (vi_notify_base + noff * vi_notify_mult);

        /* Driver OK */
        vi_common->device_status = VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER
                                 | 0x08 | VIRTIO_STATUS_DRIVER_OK;
    } else {
        /* ── Legacy I/O path ──────────────────────────────────── */
        outb(vi_iobase + VIO_STATUS, 0);
        outb(vi_iobase + VIO_STATUS, VIRTIO_STATUS_ACK);
        outb(vi_iobase + VIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

        /* Feature negotiation */
        (void)inl(vi_iobase + VIO_FEATURES);
        outl(vi_iobase + VIO_DRV_FEATURES, 0);

        /* Eventq (queue 0) */
        outw(vi_iobase + VIO_QUEUE_SEL, 0);
        uint16_t qsz = inw(vi_iobase + VIO_QUEUE_SIZE);
        if (qsz == 0 || qsz > VI_QUEUE_SIZE) qsz = VI_QUEUE_SIZE;

        vi_vq_init();

        /* Legacy: set queue PFN (page-aligned physical address / 4096) */
        outl(vi_iobase + VIO_QUEUE_PFN,
             (uint32_t)vi_eventq_mem / 4096);

        /* Driver OK */
        outb(vi_iobase + VIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER
                                   | VIRTIO_STATUS_DRIVER_OK);
    }

    /* Notify device that buffers are available */
    vi_notify();

    vi_active = 1;
    DBG("[virtio-input] Tablet ready (queue=%u)", VI_QUEUE_SIZE);
    return 1;
}

/* ═══ Event processing ═════════════════════════════════════════ */

static void vi_process_event(struct virtio_input_event *ev) {
    switch (ev->type) {
    case EV_ABS:
        if (ev->code == ABS_X) {
            int sw = gfx_is_active() ? (int)gfx_width()  : 1024;
            vi_pending_x = (int)((uint32_t)ev->value * (uint32_t)sw / VI_ABS_MAX);
            if (vi_pending_x >= sw) vi_pending_x = sw - 1;
        } else if (ev->code == ABS_Y) {
            int sh = gfx_is_active() ? (int)gfx_height() : 768;
            vi_pending_y = (int)((uint32_t)ev->value * (uint32_t)sh / VI_ABS_MAX);
            if (vi_pending_y >= sh) vi_pending_y = sh - 1;
        }
        break;

    case EV_KEY:
        if (ev->code == BTN_LEFT) {
            if (ev->value) vi_btn_state |= 0x01;
            else           vi_btn_state &= ~0x01;
        } else if (ev->code == BTN_RIGHT) {
            if (ev->value) vi_btn_state |= 0x02;
            else           vi_btn_state &= ~0x02;
        } else if (ev->code == BTN_MIDDLE) {
            if (ev->value) vi_btn_state |= 0x04;
            else           vi_btn_state &= ~0x04;
        }
        break;

    case EV_SYN:
        if (ev->code == SYN_REPORT) {
            /* Commit accumulated state */
            int x = (vi_pending_x >= 0) ? vi_pending_x : mouse_get_x();
            int y = (vi_pending_y >= 0) ? vi_pending_y : mouse_get_y();
            mouse_inject_absolute(x, y, vi_btn_state);
            vi_pending_x = vi_pending_y = -1;
        }
        break;
    }
}

void virtio_input_poll(void) {
    if (!vi_active) return;

    /* Read ISR to clear any pending interrupt (legacy path) */
    if (!vi_modern && vi_iobase)
        (void)inb(vi_iobase + VIO_ISR);

    /* Process all used buffers */
    while (vi_used->idx != vi_last_used_idx) {
        __asm__ volatile("" ::: "memory");
        uint16_t slot = vi_last_used_idx % VI_QUEUE_SIZE;
        uint16_t desc_idx = (uint16_t)vi_used->ring[slot].id;
        vi_last_used_idx++;

        if (desc_idx < VI_QUEUE_SIZE)
            vi_process_event(&vi_events[desc_idx]);

        /* Re-post the buffer */
        vi_repost(desc_idx);
    }
}

int virtio_input_active(void) {
    return vi_active;
}
