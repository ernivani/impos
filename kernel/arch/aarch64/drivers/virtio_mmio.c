/*
 * VirtIO MMIO v2 Transport — aarch64
 *
 * Shared transport layer for all VirtIO devices on QEMU virt.
 * Scans 32 MMIO slots at 0x0A000000..0x0A003E00.
 */

#include <kernel/virtio_mmio.h>
#include <kernel/io.h>
#include <string.h>

/* ═══ MMIO Register Access ══════════════════════════════════════ */

static inline uint32_t vreg_read(virtio_mmio_dev_t *dev, uint32_t offset) {
    return *(volatile uint32_t *)(dev->base + offset);
}

static inline void vreg_write(virtio_mmio_dev_t *dev, uint32_t offset, uint32_t val) {
    *(volatile uint32_t *)(dev->base + offset) = val;
}

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
            continue;  /* slot is unused */

        if (devid == device_id) {
            dev->base = base;
            dev->device_id = devid;
            dev->slot = i;
            dev->irq = VIRTIO_MMIO_IRQ_BASE + i;
            DBG("virtio-mmio: slot %u dev=%u version=%u", i, devid, version);
            return 0;
        }
    }
    return -1;
}

/* ═══ Device Initialization (VirtIO spec 4.2.3.1) ══════════════ */

int virtio_mmio_init_device(virtio_mmio_dev_t *dev, uint32_t driver_features) {
    /* VirtIO spec 4.2.3.1: Reset → ACK → DRIVER → Features → FEATURES_OK
     * Queue setup happens AFTER this call, then caller must call
     * virtio_mmio_driver_ok() to finalize. */

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

    /* 4. Read device features (word 0) */
    vreg_write(dev, VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    __asm__ volatile("dmb ish" ::: "memory");
    uint32_t dev_features = vreg_read(dev, VIRTIO_MMIO_DEVICE_FEATURES);

    /* 5. Negotiate: accept intersection of device and driver features */
    uint32_t negotiated = dev_features & driver_features;
    vreg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    __asm__ volatile("dmb ish" ::: "memory");
    vreg_write(dev, VIRTIO_MMIO_DRIVER_FEATURES, negotiated);
    __asm__ volatile("dmb ish" ::: "memory");

    /* 6. Features OK */
    vreg_write(dev, VIRTIO_MMIO_STATUS,
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_FEATURES_OK);
    __asm__ volatile("dmb ish" ::: "memory");

    /* Verify FEATURES_OK was accepted */
    uint32_t status = vreg_read(dev, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        vreg_write(dev, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Queue setup happens next. Caller MUST call virtio_mmio_driver_ok()
     * after setting up all queues. */
    return 0;
}

void virtio_mmio_driver_ok(virtio_mmio_dev_t *dev) {
    vreg_write(dev, VIRTIO_MMIO_STATUS,
               VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
               VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
    __asm__ volatile("dmb ish" ::: "memory");
}

/* ═══ Virtqueue Setup ═══════════════════════════════════════════ */

int virtio_mmio_setup_vq(virtio_mmio_dev_t *dev, uint32_t queue_idx,
                         void *mem, uint16_t size, virtqueue_t *vq) {
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

    /* Layout vring in memory:
     *   desc:  size * 16 bytes
     *   avail: 6 + size * 2 bytes (aligned to 2)
     *   used:  6 + size * 8 bytes (aligned to 4096 for MMIO) */
    memset(mem, 0, 16384);  /* zero the entire queue memory */

    uint8_t *base = (uint8_t *)mem;
    vq->desc  = (struct vring_desc *)base;
    vq->avail = (struct vring_avail *)(base + size * 16);
    /* Used ring aligned to page boundary */
    uintptr_t used_offset = (uintptr_t)(base + size * 16 + 6 + size * 2);
    used_offset = (used_offset + 4095) & ~4095UL;
    vq->used  = (struct vring_used *)used_offset;

    vq->size = size;
    vq->free_head = 0;
    vq->last_used_idx = 0;
    vq->num_free = size;

    /* Build free list */
    for (uint16_t i = 0; i < size - 1; i++)
        vq->desc[i].next = i + 1;
    vq->desc[size - 1].next = 0xFFFF;

    /* Program queue addresses (physical = virtual, identity mapped) */
    uint64_t desc_pa = (uint64_t)(uintptr_t)vq->desc;
    uint64_t avail_pa = (uint64_t)(uintptr_t)vq->avail;
    uint64_t used_pa = (uint64_t)(uintptr_t)vq->used;

    vreg_write(dev, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_pa);
    vreg_write(dev, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_pa >> 32));
    vreg_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_LOW, (uint32_t)avail_pa);
    vreg_write(dev, VIRTIO_MMIO_QUEUE_DRIVER_HIGH, (uint32_t)(avail_pa >> 32));
    vreg_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_LOW, (uint32_t)used_pa);
    vreg_write(dev, VIRTIO_MMIO_QUEUE_DEVICE_HIGH, (uint32_t)(used_pa >> 32));

    /* Enable the queue */
    vreg_write(dev, VIRTIO_MMIO_QUEUE_READY, 1);
    __asm__ volatile("dmb ish" ::: "memory");

    DBG("virtio-mmio: vq[%u] size=%u desc=0x%x avail=0x%x used=0x%x",
        queue_idx, size,
        (unsigned)(uintptr_t)vq->desc,
        (unsigned)(uintptr_t)vq->avail,
        (unsigned)(uintptr_t)vq->used);

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
    /* Spin-poll with yield, ~1000 iterations per ms (rough estimate) */
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

/* GIC Distributor Set-Enable register */
#define GICD_BASE_ADDR       0x08000000
#define GICD_ISENABLER(n)    (GICD_BASE_ADDR + 0x100 + 4*(n))
#define GICD_IPRIORITYR(n)   (GICD_BASE_ADDR + 0x400 + 4*(n))
#define GICD_ITARGETSR(n)    (GICD_BASE_ADDR + 0x800 + 4*(n))

void virtio_mmio_enable_irq(virtio_mmio_dev_t *dev) {
    uint32_t intid = dev->irq;
    /* Enable the SPI in the GIC distributor */
    uint32_t reg = intid / 32;
    uint32_t bit = intid % 32;
    mmio_write32(GICD_ISENABLER(reg), 1 << bit);

    /* Set priority to 0x80 */
    uint32_t preg = intid / 4;
    uint32_t shift = (intid % 4) * 8;
    uint32_t val = mmio_read32(GICD_IPRIORITYR(preg));
    val &= ~(0xFF << shift);
    val |= (0x80 << shift);
    mmio_write32(GICD_IPRIORITYR(preg), val);

    /* Target CPU 0 */
    uint32_t treg = intid / 4;
    uint32_t tshift = (intid % 4) * 8;
    uint32_t tval = mmio_read32(GICD_ITARGETSR(treg));
    tval &= ~(0xFF << tshift);
    tval |= (0x01 << tshift);
    mmio_write32(GICD_ITARGETSR(treg), tval);
}
