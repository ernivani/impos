#include <kernel/virtio_gpu.h>
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

/* ═══ GPU command types ════════════════════════════════════════ */

#define VIRTIO_GPU_CMD_GET_DISPLAY_INFO         0x0100
#define VIRTIO_GPU_CMD_RESOURCE_CREATE_2D       0x0101
#define VIRTIO_GPU_CMD_RESOURCE_UNREF           0x0102
#define VIRTIO_GPU_CMD_SET_SCANOUT              0x0103
#define VIRTIO_GPU_CMD_RESOURCE_FLUSH           0x0104
#define VIRTIO_GPU_CMD_TRANSFER_TO_HOST_2D      0x0105
#define VIRTIO_GPU_CMD_RESOURCE_ATTACH_BACKING  0x0106
#define VIRTIO_GPU_CMD_RESOURCE_DETACH_BACKING  0x0107

#define VIRTIO_GPU_CMD_UPDATE_CURSOR            0x0300
#define VIRTIO_GPU_CMD_MOVE_CURSOR              0x0301

#define VIRTIO_GPU_RESP_OK_NODATA               0x1100
#define VIRTIO_GPU_RESP_OK_DISPLAY_INFO         0x1101

/* Pixel format: BGRX (matches our framebuffer layout) */
#define VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM        2

/* ═══ Protocol structures ══════════════════════════════════════ */

struct virtio_gpu_ctrl_hdr {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_rect {
    uint32_t x, y, width, height;
} __attribute__((packed));

struct virtio_gpu_resource_create_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t format;
    uint32_t width;
    uint32_t height;
} __attribute__((packed));

struct virtio_gpu_resource_attach_backing {
    struct virtio_gpu_ctrl_hdr hdr;
    uint32_t resource_id;
    uint32_t nr_entries;
} __attribute__((packed));

struct virtio_gpu_mem_entry {
    uint64_t addr;
    uint32_t length;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_set_scanout {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t scanout_id;
    uint32_t resource_id;
} __attribute__((packed));

struct virtio_gpu_transfer_to_host_2d {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint64_t offset;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

struct virtio_gpu_resource_flush_cmd {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_rect r;
    uint32_t resource_id;
    uint32_t padding;
} __attribute__((packed));

/* GET_DISPLAY_INFO response */
#define VIRTIO_GPU_MAX_SCANOUTS 16

struct virtio_gpu_display_one {
    struct virtio_gpu_rect r;
    uint32_t enabled;
    uint32_t flags;
} __attribute__((packed));

struct virtio_gpu_resp_display_info {
    struct virtio_gpu_ctrl_hdr hdr;
    struct virtio_gpu_display_one pmodes[VIRTIO_GPU_MAX_SCANOUTS];
} __attribute__((packed));

struct virtio_gpu_cursor_cmd {
    struct virtio_gpu_ctrl_hdr hdr;
    struct {
        uint32_t scanout_id;
        uint32_t x;
        uint32_t y;
        uint32_t padding;
    } pos;
    uint32_t resource_id;
    uint32_t hot_x;
    uint32_t hot_y;
    uint32_t padding;
} __attribute__((packed));

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

/* ═══ Driver state ═════════════════════════════════════════════ */

static int gpu_active = 0;
static uint16_t gpu_iobase;

/* Virtqueue memory — page-aligned static arrays */
static uint8_t vq_ctrl_mem[16384] __attribute__((aligned(4096)));
static uint8_t vq_cursor_mem[16384] __attribute__((aligned(4096)));
static virtqueue_t ctrl_vq;
static virtqueue_t cursor_vq;

/* Command/response buffers (must be contiguous, identity-mapped) */
static uint8_t cmd_buf[512] __attribute__((aligned(64)));
static uint8_t resp_buf[64] __attribute__((aligned(64)));

/* Scanout state */
static uint32_t scanout_res_id;
static uint32_t cursor_res_id;
static uint32_t next_resource_id = 1;
static uint32_t disp_w, disp_h;
static uint32_t *disp_buf;
static uint32_t disp_pitch; /* bytes per row */

/* Cursor state */
#define CURSOR_W 32
#define CURSOR_H 32
static uint32_t cursor_pixels[CURSOR_W * CURSOR_H] __attribute__((aligned(64)));
static int hw_cursor_active = 0;

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
    outw(gpu_iobase + VIRTIO_REG_QUEUE_NOTIFY, queue_idx);

    /* Poll for completion */
    int timeout = 100000;
    while (vq->used->idx == vq->last_used_idx && --timeout > 0)
        __asm__ volatile("pause");

    if (timeout <= 0) {
        vq_free_desc(vq, d0);
        vq_free_desc(vq, d1);
        return -1;
    }

    vq->last_used_idx++;

    /* Free descriptors */
    vq_free_desc(vq, d0);
    vq_free_desc(vq, d1);

    /* Check response */
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp;
    return (hdr->type >= VIRTIO_GPU_RESP_OK_NODATA &&
            hdr->type <= 0x11FF) ? 0 : -1;
}

/* Submit a command with an additional data buffer chained after cmd.
   Three descriptors: cmd → data → resp */
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

    outw(gpu_iobase + VIRTIO_REG_QUEUE_NOTIFY, queue_idx);

    int timeout = 100000;
    while (vq->used->idx == vq->last_used_idx && --timeout > 0)
        __asm__ volatile("pause");

    vq->last_used_idx++;
    vq_free_desc(vq, d0);
    vq_free_desc(vq, d1);
    vq_free_desc(vq, d2);

    if (timeout <= 0) return -1;
    struct virtio_gpu_ctrl_hdr *hdr = (struct virtio_gpu_ctrl_hdr *)resp;
    return (hdr->type >= VIRTIO_GPU_RESP_OK_NODATA &&
            hdr->type <= 0x11FF) ? 0 : -1;
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

    outw(gpu_iobase + VIRTIO_REG_QUEUE_NOTIFY, 1);

    /* Brief wait for cursor queue */
    int timeout = 10000;
    while (vq->used->idx == vq->last_used_idx && --timeout > 0)
        __asm__ volatile("pause");
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

    single_entry.addr = (uint32_t)buf;
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
    if (!pci_find_device(VIRTIO_VENDOR_ID, VIRTIO_GPU_DEVICE_ID, &dev))
        return 0;

    /* Get I/O base from BAR0 (legacy VirtIO uses I/O ports) */
    gpu_iobase = dev.bar[0] & ~0x3u;
    if (gpu_iobase == 0) return 0;

    /* Enable PCI bus mastering + I/O space */
    uint16_t cmd_reg = pci_config_read_word(dev.bus, dev.device,
                                             dev.function, PCI_COMMAND);
    cmd_reg |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write_word(dev.bus, dev.device, dev.function,
                          PCI_COMMAND, cmd_reg);

    /* Reset device */
    outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS, 0);

    /* Acknowledge + driver */
    outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

    /* Read device features, accept none for basic 2D */
    inl(gpu_iobase + VIRTIO_REG_DEVICE_FEATURES);
    outl(gpu_iobase + VIRTIO_REG_DRIVER_FEATURES, 0);

    /* Initialize control virtqueue (queue 0) */
    outw(gpu_iobase + VIRTIO_REG_QUEUE_SELECT, 0);
    uint16_t ctrl_size = inw(gpu_iobase + VIRTIO_REG_QUEUE_SIZE);
    if (ctrl_size == 0 || ctrl_size > 256) ctrl_size = 128;
    vq_init(&ctrl_vq, vq_ctrl_mem, ctrl_size);
    outl(gpu_iobase + VIRTIO_REG_QUEUE_PFN,
         (uint32_t)vq_ctrl_mem >> 12);

    /* Initialize cursor virtqueue (queue 1) */
    outw(gpu_iobase + VIRTIO_REG_QUEUE_SELECT, 1);
    uint16_t cur_size = inw(gpu_iobase + VIRTIO_REG_QUEUE_SIZE);
    if (cur_size == 0 || cur_size > 256) cur_size = 128;
    vq_init(&cursor_vq, vq_cursor_mem, cur_size);
    outl(gpu_iobase + VIRTIO_REG_QUEUE_PFN,
         (uint32_t)vq_cursor_mem >> 12);

    /* Driver OK — device is live */
    outb(gpu_iobase + VIRTIO_REG_DEVICE_STATUS,
         VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
         VIRTIO_STATUS_DRIVER_OK);

    gpu_active = 1;
    DBG("[virtio-gpu] Initialized (iobase=0x%x, ctrl_q=%u, cursor_q=%u)",
        gpu_iobase, ctrl_size, cur_size);
    return 1;
}

int virtio_gpu_is_active(void) {
    return gpu_active;
}

int virtio_gpu_setup_scanout(uint32_t *backbuf, int width, int height,
                              int pitch) {
    if (!gpu_active) return 0;

    disp_w = (uint32_t)width;
    disp_h = (uint32_t)height;
    disp_buf = backbuf;
    disp_pitch = (uint32_t)pitch;

    /* Create display resource */
    scanout_res_id = next_resource_id++;
    if (gpu_create_resource_2d(scanout_res_id, VIRTIO_GPU_FORMAT_B8G8R8X8_UNORM,
                               disp_w, disp_h) != 0) {
        DBG("[virtio-gpu] Failed to create display resource");
        gpu_active = 0;
        return 0;
    }

    /* Attach backbuffer as backing storage */
    uint32_t buf_size = disp_h * disp_pitch;
    if (gpu_attach_backing(scanout_res_id, backbuf, buf_size) != 0) {
        DBG("[virtio-gpu] Failed to attach backing");
        gpu_active = 0;
        return 0;
    }

    /* Set scanout 0 to our resource */
    if (gpu_set_scanout(scanout_res_id, 0, 0, 0, disp_w, disp_h) != 0) {
        DBG("[virtio-gpu] Failed to set scanout");
        gpu_active = 0;
        return 0;
    }

    /* Initial full transfer + flush */
    gpu_transfer_2d(scanout_res_id, 0, 0, disp_w, disp_h, 0);
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

    /* Offset into the backing buffer where this rect starts */
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
