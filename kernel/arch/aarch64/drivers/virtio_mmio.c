/*
 * VirtIO MMIO Transport — aarch64
 *
 * Supports both legacy (v1) and modern (v2) MMIO transport.
 * QEMU virt machine defaults to v1 (legacy).
 *
 * Legacy register layout (v1):
 *   0x00 MagicValue, 0x04 Version, 0x08 DeviceID, 0x0C VendorID
 *   0x10 HostFeatures, 0x14 HostFeaturesSel
 *   0x20 GuestFeatures, 0x24 GuestFeaturesSel
 *   0x28 GuestPageSize, 0x30 QueueSel, 0x34 QueueNumMax
 *   0x38 QueueNum, 0x3C QueueAlign, 0x40 QueuePFN
 *   0x50 QueueNotify, 0x60 InterruptStatus, 0x64 InterruptACK
 *   0x70 Status, 0x100+ Config
 */

#include <kernel/virtio_mmio.h>
#include <kernel/io.h>
#include <string.h>

/* Legacy (v1) register offsets that differ from v2 */
#define VIRTIO_MMIO_HOST_FEATURES       0x010
#define VIRTIO_MMIO_HOST_FEATURES_SEL   0x014
#define VIRTIO_MMIO_GUEST_FEATURES      0x020
#define VIRTIO_MMIO_GUEST_FEATURES_SEL  0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE     0x028
#define VIRTIO_MMIO_QUEUE_PFN           0x040
#define VIRTIO_MMIO_QUEUE_ALIGN_REG     0x03C

/* Legacy page/alignment sizes */
#define VIRTIO_MMIO_PAGE_SIZE   4096
#define VIRTIO_MMIO_VRING_ALIGN 4096

/* ═══ MMIO Register Access ══════════════════════════════════════ */

static inline uint32_t vreg_read(virtio_mmio_dev_t *dev, uint32_t offset) {
    return *(volatile uint32_t *)(dev->base + offset);
}

static inline void vreg_write(virtio_mmio_dev_t *dev, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(dev->base + offset) = val;
}

/* Track version per device slot */
static uint32_t dev_version[VIRTIO_MMIO_SLOTS];

/* ═══ Device Discovery ══════════════════════════════════════════ */

int virtio_mmio_find(uint32_t device_id, virtio_mmio_dev_t *dev) {
    for (uint32_t i = 0; i < VIRTIO_MMIO_SLOTS; i++) {
        uintptr_t base = VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE;
        uint32_t magic = *(volatile uint32_t *)base;
        if (magic != VIRTIO_MMIO_MAGIC)
            continue;

        uint32_t version = *(volatile uint32_t *)(base + VIRTIO_MMIO_VERSION);
        uint32_t devid = *(volatile uint32_t *)(base + VIRTIO_MMIO_DEVICE_ID);
        if (devid == 0)
            continue;

        if (devid == device_id) {
            dev->base = base;
            dev->device_id = devid;
            dev->slot = i;
            dev->irq = VIRTIO_MMIO_IRQ_BASE + i;
            dev_version[i] = version;
            DBG("virtio-mmio: slot %u dev=%u version=%u", i, devid, version);
            return 0;
        }
    }
    return -1;
}

/* ═══ Device Initialization ════════════════════════════════════ */

int virtio_mmio_init_device(virtio_mmio_dev_t *dev, uint32_t driver_features) {
    uint32_t version = dev_version[dev->slot];

    /* 1. Reset device */
    vreg_write(dev, VIRTIO_MMIO_STATUS, 0);
    __asm__ volatile("dmb ish" ::: "memory");

    /* 2. Acknowledge */
    vreg_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    __asm__ volatile("dmb ish" ::: "memory");

    /* 3. Driver recognized */
    vreg_write(dev, VIRTIO_MMIO_STATUS,
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
    __asm__ volatile("dmb ish" ::: "memory");

    if (version == 1) {
        /* Legacy: read features directly, write guest features */
        uint32_t dev_features = vreg_read(dev, VIRTIO_MMIO_HOST_FEATURES);
        uint32_t negotiated = dev_features & driver_features;
        vreg_write(dev, VIRTIO_MMIO_GUEST_FEATURES, negotiated);
        __asm__ volatile("dmb ish" ::: "memory");

        /* Legacy: set guest page size */
        vreg_write(dev, VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_MMIO_PAGE_SIZE);
        __asm__ volatile("dmb ish" ::: "memory");

        /* No FEATURES_OK step in legacy */
    } else {
        /* Modern (v2): use feature select registers */
        vreg_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
        __asm__ volatile("dmb ish" ::: "memory");
        uint32_t dev_features = vreg_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);

        uint32_t negotiated = dev_features & driver_features;
        vreg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
        __asm__ volatile("dmb ish" ::: "memory");
        vreg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, negotiated);
        __asm__ volatile("dmb ish" ::: "memory");

        /* FEATURES_OK */
        vreg_write(dev, VIRTIO_MMIO_STATUS,
                   VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK);
        __asm__ volatile("dmb ish" ::: "memory");

        uint32_t status = vreg_read(dev, VIRTIO_MMIO_STATUS);
        if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
            vreg_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
            return -1;
        }
    }

    return 0;
}

void virtio_mmio_driver_ok(virtio_mmio_dev_t *dev) {
    uint32_t version = dev_version[dev->slot];
    if (version == 1) {
        /* Legacy: just OR in DRIVER_OK */
        uint32_t status = vreg_read(dev, VIRTIO_MMIO_STATUS);
        vreg_write(dev, VIRTIO_MMIO_STATUS, status | VIRTIO_STATUS_DRIVER_OK);
    } else {
        vreg_write(dev, VIRTIO_MMIO_STATUS,
                   VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    }
    __asm__ volatile("dmb ish" ::: "memory");
}

/* ═══ Vring Size Calculation (Legacy) ══════════════════════════ */

/* Legacy vring layout:
 *   desc table:  num * 16 bytes
 *   avail ring:  6 + num * 2 bytes, then pad to ALIGN
 *   used ring:   6 + num * 8 bytes
 * Total PFN = base_addr / PAGE_SIZE */
static uint32_t vring_size(uint16_t num) {
    uint32_t desc_size = num * 16;
    uint32_t avail_size = 6 + num * 2;
    uint32_t used_size = 6 + num * 8;
    uint32_t part1 = (desc_size + avail_size + VIRTIO_MMIO_VRING_ALIGN - 1)
                     & ~(VIRTIO_MMIO_VRING_ALIGN - 1);
    return part1 + used_size;
}

/* ═══ Virtqueue Setup ═══════════════════════════════════════════ */

int virtio_mmio_setup_vq(virtio_mmio_dev_t *dev, uint32_t queue_idx,
                         void *mem, uint16_t size, virtqueue_t *vq) {
    uint32_t version = dev_version[dev->slot];

    /* Select queue */
    vreg_write(dev, VIRTIO_MMIO_QUEUE_SEL, queue_idx);
    __asm__ volatile("dmb ish" ::: "memory");

    /* Check max size */
    uint32_t max_size = vreg_read(dev, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_size == 0)
        return -1;
    if (size > max_size)
        size = (uint16_t)max_size;

    /* Set queue size */
    vreg_write(dev, VIRTIO_MMIO_QUEUE_NUM, size);

    /* Zero memory */
    uint32_t total = vring_size(size);
    if (total > 16384) total = 16384;
    memset(mem, 0, total);

    /* Layout vring */
    uint8_t *base = (uint8_t *)mem;
    vq->desc  = (struct vring_desc *)base;
    vq->avail = (struct vring_avail *)(base + size * 16);

    /* Used ring: aligned to VRING_ALIGN after desc+avail */
    uintptr_t used_offset = (uintptr_t)base + size * 16 + 6 + size * 2;
    used_offset = (used_offset + VIRTIO_MMIO_VRING_ALIGN - 1)
                  & ~(uintptr_t)(VIRTIO_MMIO_VRING_ALIGN - 1);
    vq->used  = (struct vring_used *)used_offset;

    vq->size = size;
    vq->free_head = 0;
    vq->last_used_idx = 0;
    vq->num_free = size;

    /* Build free list */
    for (uint16_t i = 0; i < size - 1; i++)
        vq->desc[i].next = i + 1;
    vq->desc[size - 1].next = 0xFFFF;

    if (version == 1) {
        /* Legacy: set alignment and PFN */
        vreg_write(dev, VIRTIO_MMIO_QUEUE_ALIGN_REG, VIRTIO_MMIO_VRING_ALIGN);
        uint32_t pfn = (uint32_t)((uintptr_t)mem / VIRTIO_MMIO_PAGE_SIZE);
        vreg_write(dev, VIRTIO_MMIO_QUEUE_PFN, pfn);
    } else {
        /* Modern: separate address registers */
        uint64_t desc_pa = (uint64_t)(uintptr_t)vq->desc;
        uint64_t avail_pa = (uint64_t)(uintptr_t)vq->avail;
        uint64_t used_pa = (uint64_t)(uintptr_t)vq->used;

        vreg_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_pa);
        vreg_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_pa >> 32));
        vreg_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_pa);
        vreg_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_pa >> 32));
        vreg_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_pa);
        vreg_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_pa >> 32));

        vreg_write(dev, VIRTIO_MMIO_QUEUE_READY, 1);
    }
    __asm__ volatile("dmb ish" ::: "memory");

    DBG("virtio-mmio: vq[%u] size=%u desc=0x%x avail=0x%x used=0x%x pfn=0x%x",
        queue_idx, size,
        (unsigned)(uintptr_t)vq->desc,
        (unsigned)(uintptr_t)vq->avail,
        (unsigned)(uintptr_t)vq->used,
        (unsigned)((uintptr_t)mem / VIRTIO_MMIO_PAGE_SIZE));

    return 0;
}

/* ═══ Descriptor Management ═════════════════════════════════════ */

int vq_alloc_desc(virtqueue_t *vq) {
    if (vq->num_free == 0)
        return -1;
    uint16_t idx = vq->free_head;
    vq->free_head = vq->desc[idx].next;
    vq->num_free--;
    return idx;
}

void vq_free_desc(virtqueue_t *vq, uint16_t idx) {
    vq->desc[idx].next = vq->free_head;
    vq->desc[idx].flags = 0;
    vq->free_head = idx;
    vq->num_free++;
}

/* ═══ Notification ══════════════════════════════════════════════ */

void vq_kick(virtio_mmio_dev_t *dev, uint32_t queue_idx) {
    __asm__ volatile("dmb ish" ::: "memory");
    vreg_write(dev, VIRTIO_MMIO_QUEUE_NOTIFY, queue_idx);
}

/* ═══ Polling ═══════════════════════════════════════════════════ */

int vq_poll(virtqueue_t *vq, uint32_t *id, uint32_t *len, uint32_t timeout_ms) {
    uint32_t spins = timeout_ms * 1000;
    if (spins == 0) spins = 1000000;

    for (uint32_t i = 0; i < spins; i++) {
        __asm__ volatile("dmb ish" ::: "memory");
        if (vq->used->idx != vq->last_used_idx) {
            uint16_t uidx = vq->last_used_idx % vq->size;
            if (id)  *id  = vq->used->ring[uidx].id;
            if (len) *len = vq->used->ring[uidx].len;
            vq->last_used_idx++;
            return 1;
        }
        __asm__ volatile("yield" ::: "memory");
    }
    return 0;
}

/* ═══ GIC IRQ Enable ════════════════════════════════════════════ */

#define GICD_BASE_ADDR       0x08000000
#define GICD_ISENABLER(n)    (GICD_BASE_ADDR + 0x100 + 4*(n))
#define GICD_IPRIORITYR(n)   (GICD_BASE_ADDR + 0x400 + 4*(n))
#define GICD_ITARGETSR(n)    (GICD_BASE_ADDR + 0x800 + 4*(n))

void virtio_mmio_enable_irq(virtio_mmio_dev_t *dev) {
    uint32_t intid = dev->irq;
    uint32_t reg = intid / 32;
    uint32_t bit = intid % 32;
    mmio_write32(GICD_ISENABLER(reg), 1 << bit);

    uint32_t preg = intid / 4;
    uint32_t shift = (intid % 4) * 8;
    uint32_t val = mmio_read32(GICD_IPRIORITYR(preg));
    val &= ~(0xFF << shift);
    val |= (0x80 << shift);
    mmio_write32(GICD_IPRIORITYR(preg), val);

    uint32_t treg = intid / 4;
    uint32_t tshift = (intid % 4) * 8;
    uint32_t tval = mmio_read32(GICD_ITARGETSR(treg));
    tval &= ~(0xFF << tshift);
    tval |= (0x01 << tshift);
    mmio_write32(GICD_ITARGETSR(treg), tval);
}
