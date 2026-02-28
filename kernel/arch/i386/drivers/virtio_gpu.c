#include <kernel/virtio_gpu.h>
#include <kernel/virtio_gpu_3d.h>
#include <kernel/virtio_gpu_internal.h>
#include <kernel/pci.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Bochs VGA BGA registers ══════════════════════════════════ */

#define BGA_IOPORT_INDEX  0x01CE
#define BGA_IOPORT_DATA   0x01CF

static int bga_present = 0;

uint16_t bga_read(uint16_t index) {
    outw(BGA_IOPORT_INDEX, index);
    return inw(BGA_IOPORT_DATA);
}

void bga_write(uint16_t index, uint16_t value) {
    outw(BGA_IOPORT_INDEX, index);
    outw(BGA_IOPORT_DATA, value);
}

int bga_detect(void) {
    uint16_t id = bga_read(BGA_REG_ID);
    /* Bochs VGA returns 0xB0C0..0xB0C5 */
    if ((id & 0xFFF0) == 0xB0C0) {
        bga_present = 1;
        return 1;
    }
    bga_present = 0;
    return 0;
}

uint16_t bga_get_vram_64k(void) {
    if (!bga_present) return 0;
    return bga_read(BGA_REG_VIDEO_MEMORY_64K);
}

int bga_set_mode(int width, int height, int bpp) {
    if (!bga_present) return 0;
    bga_write(BGA_REG_ENABLE, 0);
    bga_write(BGA_REG_XRES,        (uint16_t)width);
    bga_write(BGA_REG_YRES,        (uint16_t)height);
    bga_write(BGA_REG_BPP,         (uint16_t)bpp);
    bga_write(BGA_REG_VIRT_WIDTH,  (uint16_t)width);
    bga_write(BGA_REG_VIRT_HEIGHT, (uint16_t)height);
    bga_write(BGA_REG_X_OFFSET, 0);
    bga_write(BGA_REG_Y_OFFSET, 0);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);
    return 1;
}

uint32_t bga_get_lfb_addr(void) {
    /* Bochs/QEMU VGA: vendor 0x1234, device 0x1111, LFB is at BAR0 */
    pci_device_t dev;
    if (pci_find_device(0x1234, 0x1111, &dev) != 0) return 0;
    uint32_t addr = dev.bar[0] & 0xFFFFFFF0;
    if (addr == 0) {
        /* PCI BAR not initialized (direct kernel boot without BIOS PCI setup).
           Assign the LFB to a known address and enable memory space decoding. */
        addr = 0xE0000000;
        pci_config_write_dword(dev.bus, dev.device, dev.function, PCI_BAR0, addr);
        uint16_t cmd = pci_config_read_word(dev.bus, dev.device, dev.function, 0x04);
        pci_config_write_word(dev.bus, dev.device, dev.function, 0x04, cmd | 0x02);
    }
    return addr;
}

/* ═══ VirtIO legacy PCI interface ══════════════════════════════ */

#define VIRTIO_VENDOR_ID         0x1AF4
#define VIRTIO_GPU_DEVICE_ID     0x1050

/* Legacy I/O register offsets (BAR0) */
#define VIRTIO_REG_DEVICE_FEATURES  0x00
#define VIRTIO_REG_DRIVER_FEATURES  0x04
#define VIRTIO_REG_QUEUE_PFN        0x08
#define VIRTIO_REG_QUEUE_SIZE       0x0C  /* 16-bit */
#define VIRTIO_REG_QUEUE_SELECT     0x0E  /* 16-bit */
#define VIRTIO_REG_QUEUE_NOTIFY     0x10  /* 16-bit */
#define VIRTIO_REG_DEVICE_STATUS    0x12  /* 8-bit */
#define VIRTIO_REG_ISR_STATUS       0x13  /* 8-bit */
#define VIRTIO_REG_CONFIG           0x14

/* Device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_FAILED        0x80

/* Descriptor flags */
#define VRING_DESC_F_NEXT   0x01
#define VRING_DESC_F_WRITE  0x02

/* ═══ Virtqueue structures ═════════════════════════════════════ */

struct vring_desc {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} __attribute__((packed));

struct vring_avail {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} __attribute__((packed));

struct vring_used_elem {
    uint32_t id;
    uint32_t len;
} __attribute__((packed));

struct vring_used {
    uint16_t flags;
    uint16_t idx;
    struct vring_used_elem ring[];
} __attribute__((packed));

typedef struct {
    struct vring_desc  *desc;
    struct vring_avail *avail;
    struct vring_used  *used;
    uint16_t size;
    uint16_t free_head;
    uint16_t last_used_idx;
} virtqueue_t;

/* ═══ VirtIO Modern (MMIO) structures ══════════════════════════ */

#define VIRTIO_PCI_CAP_COMMON_CFG  1
#define VIRTIO_PCI_CAP_NOTIFY_CFG  2
#define VIRTIO_PCI_CAP_ISR_CFG     3
#define VIRTIO_PCI_CAP_DEVICE_CFG  4
#define PCI_CAP_PTR                0x34
#define PCI_CAP_ID_VNDR            0x09

struct virtio_pci_common_cfg {
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

static int gpu_active = 0;
static int use_modern = 0;
static int gpu_has_virgl = 0;  /* 1 = VIRTIO_GPU_F_VIRGL negotiated */
static uint16_t gpu_iobase;

/* Modern MMIO pointers (valid when use_modern=1) */
static volatile struct virtio_pci_common_cfg *common_cfg;
static volatile uint8_t *notify_base;
static uint32_t notify_off_multiplier;
static volatile uint16_t *ctrl_notify_addr;
static volatile uint16_t *cursor_notify_addr;

/* Virtqueue memory — page-aligned static arrays */
static uint8_t vq_ctrl_mem[16384] __attribute__((aligned(4096)));
static uint8_t vq_cursor_mem[16384] __attribute__((aligned(4096)));
static virtqueue_t ctrl_vq;
static virtqueue_t cursor_vq;

/* Command/response buffers (must be contiguous, identity-mapped) */
static uint8_t cmd_buf[512] __attribute__((aligned(64)));
static uint8_t resp_buf[256] __attribute__((aligned(64)));  /* enlarged for capset responses */

/* Scanout state */
static uint32_t scanout_res_id;
static uint32_t cursor_res_id;
static uint32_t next_resource_id = 1;
static uint32_t disp_w, disp_h;
static uint32_t *disp_buf;
static uint32_t disp_pitch; /* bytes per row */
static int      use_virgl_scanout = 0; /* 1 = use 3D transfer for scanout */
static uint32_t scanout_virgl_ctx = 0; /* virgl context ID for 3D transfers */

/* Cursor state */
#define CURSOR_W 32
#define CURSOR_H 32
static uint32_t cursor_pixels[CURSOR_W * CURSOR_H] __attribute__((aligned(64)));
static int hw_cursor_active = 0;

/* ═══ Modern VirtIO PCI capability parsing ═════════════════════ */

static uint32_t bar_to_addr(pci_device_t *dev, uint8_t bar_idx) {
    if (bar_idx >= 6) return 0;
    return dev->bar[bar_idx] & ~0xFu;
}

static int virtio_parse_caps(pci_device_t *dev) {
    common_cfg = NULL;
    notify_base = NULL;
    notify_off_multiplier = 0;

    /* Check capabilities bit in PCI status register */
    uint16_t status = pci_config_read_word(dev->bus, dev->device,
                                            dev->function, PCI_STATUS);
    if (!(status & (1 << 4))) return 0; /* no capabilities list */

    uint8_t cap_ptr = pci_config_read_byte(dev->bus, dev->device,
                                            dev->function, PCI_CAP_PTR);
    cap_ptr &= 0xFC; /* align to dword */

    while (cap_ptr) {
        uint8_t cap_id = pci_config_read_byte(dev->bus, dev->device,
                                               dev->function, cap_ptr);
        uint8_t cap_next = pci_config_read_byte(dev->bus, dev->device,
                                                 dev->function, cap_ptr + 1);

        if (cap_id == PCI_CAP_ID_VNDR) {
            uint8_t cfg_type = pci_config_read_byte(dev->bus, dev->device,
                                                     dev->function, cap_ptr + 3);
            uint8_t bar_idx = pci_config_read_byte(dev->bus, dev->device,
                                                    dev->function, cap_ptr + 4);
            uint32_t offset = pci_config_read_dword(dev->bus, dev->device,
                                                     dev->function, cap_ptr + 8);
            uint32_t base = bar_to_addr(dev, bar_idx);

            if (base) {
                switch (cfg_type) {
                case VIRTIO_PCI_CAP_COMMON_CFG:
                    common_cfg = (volatile struct virtio_pci_common_cfg *)
                                 (base + offset);
                    DBG("[virtio-gpu] common_cfg at BAR%u+0x%x = 0x%x",
                        bar_idx, offset, base + offset);
                    break;
                case VIRTIO_PCI_CAP_NOTIFY_CFG:
                    notify_base = (volatile uint8_t *)(base + offset);
                    notify_off_multiplier = pci_config_read_dword(
                        dev->bus, dev->device, dev->function, cap_ptr + 16);
                    DBG("[virtio-gpu] notify at BAR%u+0x%x mult=%u",
                        bar_idx, offset, notify_off_multiplier);
                    break;
                case VIRTIO_PCI_CAP_ISR_CFG:
                    break; /* ISR: not needed for polled I/O */
                case VIRTIO_PCI_CAP_DEVICE_CFG:
                    break; /* GPU device config: not needed for basic 2D */
                }
            }
        }

        cap_ptr = cap_next;
    }

    return common_cfg != NULL && notify_base != NULL;
}

/* ═══ Notification helper ══════════════════════════════════════ */

static void gpu_notify(uint16_t queue_idx) {
    if (use_modern) {
        volatile uint16_t *addr = (queue_idx == 0)
            ? ctrl_notify_addr : cursor_notify_addr;
        *addr = queue_idx;
    } else {
        outw(gpu_iobase + VIRTIO_REG_QUEUE_NOTIFY, queue_idx);
    }
}

/* ═══ Virtqueue helpers ════════════════════════════════════════ */

static void vq_init(virtqueue_t *vq, void *mem, uint16_t size) {
    memset(mem, 0, 16384);
    vq->size = size;
    vq->desc = (struct vring_desc *)mem;
    /* Available ring starts after descriptors */
    vq->avail = (struct vring_avail *)((uint8_t *)mem + size * 16);
    /* Used ring starts at next page-aligned offset after available ring */
    uint32_t avail_end = size * 16 + 4 + size * 2 + 2;
    uint32_t used_offset = (avail_end + 4095) & ~4095u;
    vq->used = (struct vring_used *)((uint8_t *)mem + used_offset);
    vq->free_head = 0;
    vq->last_used_idx = 0;

    /* Tell device not to generate interrupts — we use polling */
    vq->avail->flags = 1;  /* VRING_AVAIL_F_NO_INTERRUPT */

    /* Build free list */
    for (uint16_t i = 0; i < size - 1; i++) {
        vq->desc[i].next = i + 1;
        vq->desc[i].flags = VRING_DESC_F_NEXT;
    }
    vq->desc[size - 1].next = 0;
    vq->desc[size - 1].flags = 0;
}

static uint16_t vq_alloc_desc(virtqueue_t *vq) {
    uint16_t idx = vq->free_head;
    vq->free_head = vq->desc[idx].next;
    return idx;
}

static void vq_free_desc(virtqueue_t *vq, uint16_t idx) {
    vq->desc[idx].next = vq->free_head;
    vq->desc[idx].flags = VRING_DESC_F_NEXT;
    vq->free_head = idx;
}

/* Submit a command (cmd_buf) and wait for response (resp_buf).
   cmd_len = bytes of command, resp_len = bytes to receive. */
static int vq_submit_cmd(virtqueue_t *vq, uint16_t queue_idx,
                         void *cmd, uint32_t cmd_len,
                         void *resp, uint32_t resp_len) {
    /* Allocate two descriptors: one for cmd (device-readable),
       one for resp (device-writable) */
    uint16_t d0 = vq_alloc_desc(vq);
    uint16_t d1 = vq_alloc_desc(vq);

    /* Command descriptor (device reads) */
    vq->desc[d0].addr = (uint32_t)cmd;
    vq->desc[d0].len = cmd_len;
    vq->desc[d0].flags = VRING_DESC_F_NEXT;
    vq->desc[d0].next = d1;

    /* Response descriptor (device writes) */
    vq->desc[d1].addr = (uint32_t)resp;
    vq->desc[d1].len = resp_len;
    vq->desc[d1].flags = VRING_DESC_F_WRITE;
    vq->desc[d1].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = d0;
    __asm__ volatile("" ::: "memory");  /* memory barrier */
    vq->avail->idx = avail_idx + 1;

    /* Notify device */
    gpu_notify(queue_idx);

    /* Poll for completion.  With virgl enabled the host GL context may
       take hundreds of milliseconds to initialise, so use a generous
       spin budget (~500 ms at ~5 ns/pause under KVM). */
    for (int tries = 0; tries < 100; tries++) {
        int spins = 1000000;
        while (vq->used->idx == vq->last_used_idx && --spins > 0)
            __asm__ volatile("pause" ::: "memory");
        if (vq->used->idx != vq->last_used_idx)
            break;
    }

    if (vq->used->idx == vq->last_used_idx) {
        DBG("[virtio-gpu] vq_submit_cmd TIMEOUT (q=%u cmd_len=%u resp_len=%u)",
            queue_idx, cmd_len, resp_len);
        /* Do NOT free descriptors — device may still reference them.
           Advance last_used_idx to resync when device eventually
           completes, and accept the descriptor leak. */
        return -1;
    }

    vq->last_used_idx++;

    /* Free descriptors */
    vq_free_desc(vq, d0);
    vq_free_desc(vq, d1);

    /* Check response */
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp;
    struct virtio_gpu_ctrl_hdr *req = (struct virtio_gpu_ctrl_hdr *)cmd;
    if (hdr->type < VIRTIO_GPU_RESP_OK_NODATA || hdr->type > 0x11FF) {
        DBG("[virtio-gpu] cmd 0x%x FAILED: resp=0x%x", req->type, hdr->type);
        return -1;
    }
    return 0;
}

/* Submit a command with an additional data buffer chained after cmd.
   Three descriptors: cmd -> data -> resp */
static int vq_submit_cmd_data(virtqueue_t *vq, uint16_t queue_idx,
                              void *cmd, uint32_t cmd_len,
                              void *data, uint32_t data_len,
                              void *resp, uint32_t resp_len) {
    uint16_t d0 = vq_alloc_desc(vq);
    uint16_t d1 = vq_alloc_desc(vq);
    uint16_t d2 = vq_alloc_desc(vq);

    vq->desc[d0].addr = (uint32_t)cmd;
    vq->desc[d0].len = cmd_len;
    vq->desc[d0].flags = VRING_DESC_F_NEXT;
    vq->desc[d0].next = d1;

    vq->desc[d1].addr = (uint32_t)data;
    vq->desc[d1].len = data_len;
    vq->desc[d1].flags = VRING_DESC_F_NEXT;
    vq->desc[d1].next = d2;

    vq->desc[d2].addr = (uint32_t)resp;
    vq->desc[d2].len = resp_len;
    vq->desc[d2].flags = VRING_DESC_F_WRITE;
    vq->desc[d2].next = 0;

    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = d0;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx = avail_idx + 1;

    gpu_notify(queue_idx);

    for (int tries = 0; tries < 100; tries++) {
        int spins = 1000000;
        while (vq->used->idx == vq->last_used_idx && --spins > 0)
            __asm__ volatile("pause" ::: "memory");
        if (vq->used->idx != vq->last_used_idx)
            break;
    }

    if (vq->used->idx == vq->last_used_idx) {
        DBG("[virtio-gpu] vq_submit_cmd_data TIMEOUT (q=%u)", queue_idx);
        return -1;
    }

    vq->last_used_idx++;
    vq_free_desc(vq, d0);
    vq_free_desc(vq, d1);
    vq_free_desc(vq, d2);
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp;
    struct virtio_gpu_ctrl_hdr *req = (struct virtio_gpu_ctrl_hdr *)cmd;
    if (hdr->type < VIRTIO_GPU_RESP_OK_NODATA || hdr->type > 0x11FF) {
        DBG("[virtio-gpu] cmd_data 0x%x FAILED: resp=0x%x", req->type, hdr->type);
        return -1;
    }
    return 0;
}

/* Submit a cursor command (no response expected, fire-and-forget) */
static void vq_submit_cursor(void *cmd, uint32_t cmd_len) {
    virtqueue_t *vq = &cursor_vq;
    uint16_t d0 = vq_alloc_desc(vq);

    vq->desc[d0].addr = (uint32_t)cmd;
    vq->desc[d0].len = cmd_len;
    vq->desc[d0].flags = 0;
    vq->desc[d0].next = 0;

    uint16_t avail_idx = vq->avail->idx;
    vq->avail->ring[avail_idx % vq->size] = d0;
    __asm__ volatile("" ::: "memory");
    vq->avail->idx = avail_idx + 1;

    gpu_notify(1);

    /* Brief wait for cursor queue */
    int timeout = 100000;
    while (vq->used->idx == vq->last_used_idx && --timeout > 0)
        __asm__ volatile("pause" ::: "memory");
    if (vq->used->idx != vq->last_used_idx)
        vq->last_used_idx = vq->used->idx;

    vq_free_desc(vq, d0);
}

/* ═══ GPU command wrappers ═════════════════════════════════════ */

static int gpu_create_resource_2d(uint32_t res_id, uint32_t format,
                                   uint32_t w, uint32_t h) {
    struct virtio_gpu_resource_create_2d *cmd =
        (struct virtio_gpu_resource_create_2d *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_CREATE_2D;
    cmd->resource_id = res_id;
    cmd->format = format;
    cmd->width = w;
    cmd->height = h;
    return vq_submit_cmd(&ctrl_vq, 0, cmd, sizeof(*cmd),
                         resp_buf, sizeof(struct virtio_gpu_ctrl_hdr));
}

/* Single contiguous backing region */
static struct virtio_gpu_mem_entry single_entry __attribute__((aligned(16)));

static int gpu_attach_backing(uint32_t res_id, uint32_t *buf,
                               uint32_t size_bytes) {
    struct virtio_gpu_resource_attach_backing *cmd =
        (struct virtio_gpu_resource_attach_backing *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING;
    cmd->resource_id = res_id;
    cmd->nr_entries = 1;

    single_entry.addr = (uint64_t)(uint32_t)buf;
    single_entry.length = size_bytes;
    single_entry.padding = 0;

    return vq_submit_cmd_data(&ctrl_vq, 0, cmd, sizeof(*cmd),
                              &single_entry, sizeof(single_entry),
                              resp_buf, sizeof(struct virtio_gpu_ctrl_hdr));
}

static int gpu_set_scanout(uint32_t res_id, uint32_t scanout_id,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct virtio_gpu_set_scanout *cmd =
        (struct virtio_gpu_set_scanout *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_SET_SCANOUT;
    cmd->r.x = x; cmd->r.y = y;
    cmd->r.width = w; cmd->r.height = h;
    cmd->scanout_id = scanout_id;
    cmd->resource_id = res_id;
    return vq_submit_cmd(&ctrl_vq, 0, cmd, sizeof(*cmd),
                         resp_buf, sizeof(struct virtio_gpu_ctrl_hdr));
}

static int gpu_transfer_2d(uint32_t res_id,
                            uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                            uint64_t offset) {
    struct virtio_gpu_transfer_to_host_2d *cmd =
        (struct virtio_gpu_transfer_to_host_2d *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D;
    cmd->r.x = x; cmd->r.y = y;
    cmd->r.width = w; cmd->r.height = h;
    cmd->offset = offset;
    cmd->resource_id = res_id;
    return vq_submit_cmd(&ctrl_vq, 0, cmd, sizeof(*cmd),
                         resp_buf, sizeof(struct virtio_gpu_ctrl_hdr));
}

static int gpu_resource_flush(uint32_t res_id,
                               uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    struct virtio_gpu_resource_flush_cmd *cmd =
        (struct virtio_gpu_resource_flush_cmd *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->hdr.type = VIRTIO_GPU_CMD_RESOURCE_FLUSH;
    cmd->r.x = x; cmd->r.y = y;
    cmd->r.width = w; cmd->r.height = h;
    cmd->resource_id = res_id;
    return vq_submit_cmd(&ctrl_vq, 0, cmd, sizeof(*cmd),
                         resp_buf, sizeof(struct virtio_gpu_ctrl_hdr));
}

/* ═══ Public API ═══════════════════════════════════════════════ */

int virtio_gpu_init(void) {
    /* Always try BGA detection first */
    bga_detect();

    /* Find VirtIO GPU PCI device */
    pci_device_t dev;
    if (pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID, &dev) != 0) {
        DBG("[virtio-gpu] PCI device %04x:%04x not found",
            VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID);
        return 0;
    }

    DBG("[virtio-gpu] PCI %d:%d.%d BAR[0]=0x%x [1]=0x%x [2]=0x%x [3]=0x%x [4]=0x%x [5]=0x%x",
        dev.bus, dev.device, dev.function,
        dev.bar[0], dev.bar[1], dev.bar[2],
        dev.bar[3], dev.bar[4], dev.bar[5]);

    /* Try legacy I/O BAR first (standalone virtio-gpu-pci) */
    gpu_iobase = 0;
    for (int i = 0; i < 6; i++) {
        if (dev.bar[i] & 0x1) {
            gpu_iobase = (uint16_t)(dev.bar[i] & ~0x3u);
            break;
        }
    }

    /* If no I/O BAR, try modern MMIO via PCI capabilities */
    if (gpu_iobase == 0) {
        if (!virtio_parse_caps(&dev)) {
            DBG("[virtio-gpu] No I/O BAR and no modern caps found");
            return 0;
        }
        use_modern = 1;
        DBG("[virtio-gpu] Using modern MMIO path");
    }

    /* Enable PCI bus mastering + I/O + memory space + disable INTx */
    uint16_t cmd_reg = pci_config_read_word(dev.bus, dev.device,
                                             dev.function, PCI_COMMAND);
    cmd_reg |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER
             | PCI_COMMAND_INTX_DISABLE;
    pci_config_write_word(dev.bus, dev.device, dev.function,
                          PCI_COMMAND, cmd_reg);
    uint8_t irq_line = pci_config_read_byte(dev.bus, dev.device,
                                             dev.function, PCI_INTERRUPT_LINE);
    DBG("[virtio-gpu] PCI IRQ line=%u, cmd=0x%x (INTx %s)",
        irq_line, cmd_reg, (cmd_reg & PCI_COMMAND_INTX_DISABLE) ? "disabled" : "ENABLED");

    /* ── Device initialization sequence ─────────────────────── */

    if (use_modern) {
        /* Reset */
        common_cfg->device_status = 0;
        __asm__ volatile("" ::: "memory");

        /* Acknowledge */
        common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE;
        /* Driver */
        common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE |
                                    VIRTIO_STATUS_DRIVER;

        /* Feature negotiation — check for VIRGL support.
         * NOTE: We intentionally do NOT negotiate VIRGL for the
         * scanout display path.  When virgl is negotiated, QEMU
         * routes ALL commands (including SET_SCANOUT, RESOURCE_FLUSH)
         * through virgl-specific GL handlers that use
         * dpy_gl_scanout_texture / dpy_gl_scanout_flush.  These
         * require a fully working virgl GL pipeline which has
         * compatibility issues across QEMU versions.
         *
         * Without virgl, the standard 2D handlers create pixman
         * surfaces that work with any display backend (including
         * virtio-vga-gl with -display gtk,gl=on).
         *
         * 3D rendering can be added later as an overlay once the
         * base display pipeline is stable. */
        common_cfg->device_feature_select = 0;
        uint32_t dev_features = common_cfg->device_feature;
        DBG("[virtio-gpu] Device features[0]: 0x%x (VIRGL=%d EDID=%d)",
            dev_features,
            !!(dev_features & (1u << VIRTIO_GPU_F_VIRGL)),
            !!(dev_features & (1u << VIRTIO_GPU_F_EDID)));

        /* Also check feature bits 32-63 */
        common_cfg->device_feature_select = 1;
        uint32_t dev_features_hi = common_cfg->device_feature;
        if (dev_features_hi)
            DBG("[virtio-gpu] Device features[1]: 0x%x", dev_features_hi);

        /* Negotiate device-specific features (bits 0-31) */
        uint32_t drv_features = 0;
        if (dev_features & (1u << VIRTIO_GPU_F_VIRGL)) {
            drv_features |= (1u << VIRTIO_GPU_F_VIRGL);
            gpu_has_virgl = 1;
            DBG("[virtio-gpu] Negotiating VIRGL feature");
        }
        if (dev_features & (1u << VIRTIO_GPU_F_EDID)) {
            drv_features |= (1u << VIRTIO_GPU_F_EDID);
        }

        common_cfg->driver_feature_select = 0;
        common_cfg->driver_feature = drv_features;

        /* Negotiate transport features (bits 32-63).
           VIRTIO_F_VERSION_1 (bit 32) is REQUIRED for modern devices.
           Without it, QEMU's virglrenderer may reject 3D commands. */
#define VIRTIO_F_VERSION_1_BIT  0   /* bit 32 = bit 0 of high word */
        uint32_t drv_features_hi = 0;
        if (dev_features_hi & (1u << VIRTIO_F_VERSION_1_BIT)) {
            drv_features_hi |= (1u << VIRTIO_F_VERSION_1_BIT);
        }

        common_cfg->driver_feature_select = 1;
        common_cfg->driver_feature = drv_features_hi;
        DBG("[virtio-gpu] Negotiated features: lo=0x%x hi=0x%x",
            drv_features, drv_features_hi);

        common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE |
                                    VIRTIO_STATUS_DRIVER |
                                    VIRTIO_STATUS_FEATURES_OK;
        __asm__ volatile("" ::: "memory");

        if (!(common_cfg->device_status & VIRTIO_STATUS_FEATURES_OK)) {
            DBG("[virtio-gpu] FEATURES_OK not set by device");
            return 0;
        }

        /* Disable MSI-X globally (we use polling, not interrupts) */
        common_cfg->msix_config = 0xFFFF;

        /* Initialize control virtqueue (queue 0) */
        common_cfg->queue_select = 0;
        uint16_t ctrl_size = common_cfg->queue_size;
        if (ctrl_size == 0 || ctrl_size > 256) ctrl_size = 128;
        common_cfg->queue_size = ctrl_size;
        vq_init(&ctrl_vq, vq_ctrl_mem, ctrl_size);

        common_cfg->queue_desc_lo   = (uint32_t)ctrl_vq.desc;
        common_cfg->queue_desc_hi   = 0;
        common_cfg->queue_driver_lo = (uint32_t)ctrl_vq.avail;
        common_cfg->queue_driver_hi = 0;
        common_cfg->queue_device_lo = (uint32_t)ctrl_vq.used;
        common_cfg->queue_device_hi = 0;
        common_cfg->queue_msix_vector = 0xFFFF; /* no MSI-X for queue 0 */
        common_cfg->queue_enable    = 1;

        uint16_t ctrl_noff = common_cfg->queue_notify_off;
        ctrl_notify_addr = (volatile uint16_t *)
            (notify_base + ctrl_noff * notify_off_multiplier);

        /* Initialize cursor virtqueue (queue 1) */
        common_cfg->queue_select = 1;
        uint16_t cur_size = common_cfg->queue_size;
        if (cur_size == 0 || cur_size > 256) cur_size = 128;
        common_cfg->queue_size = cur_size;
        vq_init(&cursor_vq, vq_cursor_mem, cur_size);

        common_cfg->queue_desc_lo   = (uint32_t)cursor_vq.desc;
        common_cfg->queue_desc_hi   = 0;
        common_cfg->queue_driver_lo = (uint32_t)cursor_vq.avail;
        common_cfg->queue_driver_hi = 0;
        common_cfg->queue_device_lo = (uint32_t)cursor_vq.used;
        common_cfg->queue_device_hi = 0;
        common_cfg->queue_msix_vector = 0xFFFF; /* no MSI-X for queue 1 */
        common_cfg->queue_enable    = 1;

        uint16_t cur_noff = common_cfg->queue_notify_off;
        cursor_notify_addr = (volatile uint16_t *)
            (notify_base + cur_noff * notify_off_multiplier);

        /* Driver OK */
        common_cfg->device_status = VIRTIO_STATUS_ACKNOWLEDGE |
                                    VIRTIO_STATUS_DRIVER |
                                    VIRTIO_STATUS_FEATURES_OK |
                                    VIRTIO_STATUS_DRIVER_OK;

        gpu_active = 1;
        DBG("[virtio-gpu] Modern MMIO init OK (ctrl_q=%u, cursor_q=%u, virgl=%d)",
            ctrl_size, cur_size, gpu_has_virgl);
    } else {
        /* ── Legacy I/O path ─────────────────────────────────── */

        /* Reset */
        outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS, 0);

        /* Acknowledge + driver */
        outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE);
        outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

        /* Feature negotiation */
        uint32_t dev_features = inl(gpu_iobase + VIRTIO_REG_DEVICE_FEATURES);
        uint32_t drv_features = 0;

        DBG("[virtio-gpu] Legacy device features: 0x%x (VIRGL=%d)",
            dev_features, !!(dev_features & (1u << VIRTIO_GPU_F_VIRGL)));

        if (dev_features & (1u << VIRTIO_GPU_F_VIRGL)) {
            drv_features |= (1u << VIRTIO_GPU_F_VIRGL);
            gpu_has_virgl = 1;
            DBG("[virtio-gpu] Negotiating VIRGL (legacy path)");
        } else {
            DBG("[virtio-gpu] 2D-only (legacy path)");
        }

        outl(gpu_iobase + VIRTIO_REG_DRIVER_FEATURES, drv_features);

        /* Control virtqueue (queue 0) */
        outw(gpu_iobase + VIRTIO_REG_QUEUE_SELECT, 0);
        uint16_t ctrl_size = inw(gpu_iobase + VIRTIO_REG_QUEUE_SIZE);
        if (ctrl_size == 0 || ctrl_size > 256) ctrl_size = 128;
        vq_init(&ctrl_vq, vq_ctrl_mem, ctrl_size);
        outl(gpu_iobase + VIRTIO_REG_QUEUE_PFN,
             (uint32_t)vq_ctrl_mem >> 12);

        /* Cursor virtqueue (queue 1) */
        outw(gpu_iobase + VIRTIO_REG_QUEUE_SELECT, 1);
        uint16_t cur_size = inw(gpu_iobase + VIRTIO_REG_QUEUE_SIZE);
        if (cur_size == 0 || cur_size > 256) cur_size = 128;
        vq_init(&cursor_vq, vq_cursor_mem, cur_size);
        outl(gpu_iobase + VIRTIO_REG_QUEUE_PFN,
             (uint32_t)vq_cursor_mem >> 12);

        /* Driver OK */
        outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS,
             VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
             VIRTIO_STATUS_DRIVER_OK);

        gpu_active = 1;
        DBG("[virtio-gpu] Legacy I/O init OK (iobase=0x%x, ctrl_q=%u, cursor_q=%u, virgl=%d)",
            gpu_iobase, ctrl_size, cur_size, gpu_has_virgl);
    }

    /* Diagnostic: query display info immediately to verify device responds */
    {
        uint32_t dw[4], dh[4];
        int n = virtio_gpu_get_display_info(dw, dh, 4);
        if (n > 0) {
            for (int i = 0; i < n; i++)
                DBG("[virtio-gpu] Scanout %d: %ux%u", i, dw[i], dh[i]);
        } else {
            DBG("[virtio-gpu] WARNING: GET_DISPLAY_INFO returned 0 scanouts");
        }
    }

    return 1;
}

int virtio_gpu_is_active(void) {
    return gpu_active;
}

int virtio_gpu_has_virgl(void) {
    return gpu_has_virgl;
}

int virtio_gpu_setup_scanout(uint32_t *backbuf, int width, int height,
                              int pitch) {
    if (!gpu_active) return 0;

    disp_w = (uint32_t)width;
    disp_h = (uint32_t)height;
    disp_buf = backbuf;
    disp_pitch = (uint32_t)pitch;

    scanout_res_id = next_resource_id++;
    uint32_t buf_size = disp_h * disp_pitch;

    /* Always use the 2D display path for scanout.  Even when virgl is
     * negotiated, we use RESOURCE_CREATE_2D + TRANSFER_TO_HOST_2D for
     * the primary display.  This works reliably with ALL QEMU display
     * backends (including virtio-vga-gl with broken GL scanout on WSL2).
     *
     * Virgl 3D commands remain available for off-screen rendering via
     * the DRM/virgl_test paths.  Only the scanout surface is 2D.
     *
     * The old 3D scanout approach used dpy_gl_scanout_texture() which
     * requires DMABUF support and a working GL display — often broken
     * on WSL2 and headless setups. */
    (void)use_virgl_scanout;
    {
setup_2d:
        /* ── Standard 2D path ──────────────────────────────────── */
        if (gpu_create_resource_2d(scanout_res_id,
                                    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                                    disp_w, disp_h) != 0) {
            DBG("[virtio-gpu] Failed to create display resource");
            gpu_active = 0;
            return 0;
        }

        if (gpu_attach_backing(scanout_res_id, backbuf, buf_size) != 0) {
            DBG("[virtio-gpu] Failed to attach backing");
            gpu_active = 0;
            return 0;
        }

        if (gpu_set_scanout(scanout_res_id, 0, 0, 0, disp_w, disp_h) != 0) {
            DBG("[virtio-gpu] Failed to set scanout");
            gpu_active = 0;
            return 0;
        }

        /* Initial full transfer via 2D path */
        gpu_transfer_2d(scanout_res_id, 0, 0, disp_w, disp_h, 0);
    }

    gpu_resource_flush(scanout_res_id, 0, 0, disp_w, disp_h);

    DBG("[virtio-gpu] Scanout %ux%u ready (resource %u)",
        disp_w, disp_h, scanout_res_id);
    return 1;
}

void virtio_gpu_transfer_2d(int x, int y, int w, int h) {
    if (!gpu_active || !scanout_res_id) return;

    /* Clip */
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)disp_w) w = (int)disp_w - x;
    if (y + h > (int)disp_h) h = (int)disp_h - y;
    if (w <= 0 || h <= 0) return;

    /* Always use 2D transfer for scanout (works with all display backends) */
    uint64_t offset = (uint64_t)y * disp_pitch + (uint64_t)x * 4;
    gpu_transfer_2d(scanout_res_id, (uint32_t)x, (uint32_t)y,
                    (uint32_t)w, (uint32_t)h, offset);
}

void virtio_gpu_flush(int x, int y, int w, int h) {
    if (!gpu_active || !scanout_res_id) return;

    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > (int)disp_w) w = (int)disp_w - x;
    if (y + h > (int)disp_h) h = (int)disp_h - y;
    if (w <= 0 || h <= 0) return;

    gpu_resource_flush(scanout_res_id, (uint32_t)x, (uint32_t)y,
                       (uint32_t)w, (uint32_t)h);
}

void virtio_gpu_flip_rects(int *rects, int count) {
    if (!gpu_active || !scanout_res_id) return;
    for (int i = 0; i < count; i++) {
        int x = rects[i * 4 + 0];
        int y = rects[i * 4 + 1];
        int w = rects[i * 4 + 2];
        int h = rects[i * 4 + 3];
        virtio_gpu_transfer_2d(x, y, w, h);
        virtio_gpu_flush(x, y, w, h);
    }
}

/* ═══ Display info query ═══════════════════════════════════════ */

/* Response buffer for display info — large struct, keep static */
static struct virtio_gpu_resp_display_info disp_info_resp
    __attribute__((aligned(64)));

int virtio_gpu_get_display_info(uint32_t *widths, uint32_t *heights,
                                 int max_scanouts) {
    if (!gpu_active || max_scanouts <= 0) return 0;

    /* Send GET_DISPLAY_INFO command */
    struct virtio_gpu_ctrl_hdr *cmd = (struct virtio_gpu_ctrl_hdr *)cmd_buf;
    memset(cmd, 0, sizeof(*cmd));
    cmd->type = VIRTIO_GPU_CMD_GET_DISPLAY_INFO;

    memset(&disp_info_resp, 0, sizeof(disp_info_resp));
    int rc = vq_submit_cmd(&ctrl_vq, 0, cmd, sizeof(*cmd),
                           &disp_info_resp, sizeof(disp_info_resp));
    if (rc != 0) return 0;
    if (disp_info_resp.hdr.type != VIRTIO_GPU_RESP_OK_DISPLAY_INFO) return 0;

    int count = 0;
    for (int i = 0; i < VIRTIO_GPU_MAX_SCANOUTS && count < max_scanouts; i++) {
        if (disp_info_resp.pmodes[i].enabled &&
            disp_info_resp.pmodes[i].r.width > 0 &&
            disp_info_resp.pmodes[i].r.height > 0) {
            widths[count] = disp_info_resp.pmodes[i].r.width;
            heights[count] = disp_info_resp.pmodes[i].r.height;
            count++;
        }
    }
    return count;
}

/* ═══ Hardware cursor ══════════════════════════════════════════ */

void virtio_gpu_set_cursor(uint32_t *pixels, int w, int h,
                            int hot_x, int hot_y) {
    if (!gpu_active) return;

    if (!pixels) {
        /* Disable cursor */
        if (hw_cursor_active) {
            static struct virtio_gpu_cursor_cmd cmd;
            memset(&cmd, 0, sizeof(cmd));
            cmd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
            cmd.pos.scanout_id = 0;
            cmd.resource_id = 0;  /* resource 0 = disable */
            vq_submit_cursor(&cmd, sizeof(cmd));
            hw_cursor_active = 0;
        }
        return;
    }

    /* Create cursor resource if needed */
    if (!cursor_res_id) {
        cursor_res_id = next_resource_id++;
        if (gpu_create_resource_2d(cursor_res_id,
                                    VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                                    CURSOR_W, CURSOR_H) != 0)
            return;

        /* Attach cursor_pixels as backing */
        if (gpu_attach_backing(cursor_res_id, cursor_pixels,
                               CURSOR_W * CURSOR_H * 4) != 0)
            return;
    }

    /* Copy pixels into cursor buffer (clamp to 32x32) */
    memset(cursor_pixels, 0, sizeof(cursor_pixels));
    int cw = w < CURSOR_W ? w : CURSOR_W;
    int ch = h < CURSOR_H ? h : CURSOR_H;
    for (int cy = 0; cy < ch; cy++)
        memcpy(&cursor_pixels[cy * CURSOR_W], &pixels[cy * w], (size_t)cw * 4);

    /* Transfer cursor data to host */
    gpu_transfer_2d(cursor_res_id, 0, 0, CURSOR_W, CURSOR_H, 0);

    /* Send UPDATE_CURSOR */
    static struct virtio_gpu_cursor_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_UPDATE_CURSOR;
    cmd.pos.scanout_id = 0;
    cmd.resource_id = cursor_res_id;
    cmd.hot_x = (uint32_t)hot_x;
    cmd.hot_y = (uint32_t)hot_y;
    vq_submit_cursor(&cmd, sizeof(cmd));

    hw_cursor_active = 1;
}

void virtio_gpu_move_cursor(int x, int y) {
    if (!gpu_active || !hw_cursor_active) return;

    static struct virtio_gpu_cursor_cmd cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.hdr.type = VIRTIO_GPU_CMD_MOVE_CURSOR;
    cmd.pos.scanout_id = 0;
    cmd.pos.x = (uint32_t)x;
    cmd.pos.y = (uint32_t)y;
    cmd.resource_id = cursor_res_id;
    vq_submit_cursor(&cmd, sizeof(cmd));
}

uint32_t virtio_gpu_vram_kb(void) {
    /* Check BGA VRAM first */
    if (bga_present) {
        uint16_t blocks = bga_get_vram_64k();
        if (blocks > 0)
            return (uint32_t)blocks * 64;
    }
    /* Fallback: compute from display dimensions */
    if (gpu_active)
        return (disp_w * disp_h * 4) / 1024;
    return 0;
}

/* ═══ Public helpers for 3D module ════════════════════════════ */

uint32_t virtio_gpu_alloc_resource_id(void) {
    return next_resource_id++;
}

int virtio_gpu_submit_ctrl_cmd(void *cmd, uint32_t cmd_len,
                                void *resp, uint32_t resp_len) {
    if (!gpu_active) return -1;
    return vq_submit_cmd(&ctrl_vq, 0, cmd, cmd_len, resp, resp_len);
}

int virtio_gpu_submit_ctrl_cmd_data(void *cmd, uint32_t cmd_len,
                                     void *data, uint32_t data_len,
                                     void *resp, uint32_t resp_len) {
    if (!gpu_active) return -1;
    return vq_submit_cmd_data(&ctrl_vq, 0, cmd, cmd_len,
                              data, data_len, resp, resp_len);
}

int virtio_gpu_attach_resource_backing(uint32_t res_id,
                                        uint32_t *buf, uint32_t size_bytes) {
    return gpu_attach_backing(res_id, buf, size_bytes);
}

int virtio_gpu_set_scanout_resource(uint32_t res_id, uint32_t scanout_id,
                                     uint32_t x, uint32_t y,
                                     uint32_t w, uint32_t h) {
    return gpu_set_scanout(res_id, scanout_id, x, y, w, h);
}

int virtio_gpu_flush_resource(uint32_t res_id,
                               uint32_t x, uint32_t y,
                               uint32_t w, uint32_t h) {
    return gpu_resource_flush(res_id, x, y, w, h);
}
