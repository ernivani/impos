/*
 * VirtIO MMIO v2 Transport Layer
 *
 * Shared transport for all VirtIO device drivers on aarch64.
 * QEMU virt machine exposes 32 MMIO slots at 0x0A000000 + n*0x200,
 * each wired to GIC SPI (INTID = 48 + n).
 */
#ifndef _KERNEL_VIRTIO_MMIO_H
#define _KERNEL_VIRTIO_MMIO_H

#include <stdint.h>

/* ═══ MMIO Register Offsets (VirtIO MMIO v2) ═════════════════════ */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
#define VIRTIO_MMIO_VERSION             0x004
#define VIRTIO_MMIO_DEVICE_ID           0x008
#define VIRTIO_MMIO_VENDOR_ID           0x00C
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL           0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034
#define VIRTIO_MMIO_QUEUE_NUM           0x038
#define VIRTIO_MMIO_QUEUE_READY         0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
#define VIRTIO_MMIO_STATUS              0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW    0x090
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH   0x094
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW    0x0A0
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH   0x0A4
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0FC
#define VIRTIO_MMIO_CONFIG              0x100

/* Magic value: "virt" in little-endian */
#define VIRTIO_MMIO_MAGIC   0x74726976

/* Device status bits */
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_NEEDS_RESET   0x40
#define VIRTIO_STATUS_FAILED        0x80

/* Descriptor flags */
#define VRING_DESC_F_NEXT   0x01
#define VRING_DESC_F_WRITE  0x02

/* Device IDs */
#define VIRTIO_DEV_NET      1
#define VIRTIO_DEV_BLK      2
#define VIRTIO_DEV_CONSOLE  3
#define VIRTIO_DEV_RNG      4
#define VIRTIO_DEV_GPU      16
#define VIRTIO_DEV_INPUT    18

/* QEMU virt MMIO slot layout */
#define VIRTIO_MMIO_BASE    0x0A000000
#define VIRTIO_MMIO_STRIDE  0x200
#define VIRTIO_MMIO_SLOTS   32
#define VIRTIO_MMIO_IRQ_BASE 48  /* SPI INTID = 48 + slot */

/* ═══ Vring Structures ═══════════════════════════════════════════ */

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

/* ═══ Virtqueue Handle ═══════════════════════════════════════════ */

typedef struct {
    struct vring_desc  *desc;
    struct vring_avail *avail;
    struct vring_used  *used;
    uint16_t size;       /* number of descriptors */
    uint16_t free_head;  /* head of free descriptor list */
    uint16_t last_used_idx;
    uint16_t num_free;   /* free descriptors remaining */
} virtqueue_t;

/* ═══ Device Handle ═════════════════════════════════════════════ */

typedef struct {
    uintptr_t  base;       /* MMIO base address */
    uint32_t   device_id;  /* VirtIO device ID */
    uint32_t   irq;        /* GIC INTID */
    uint32_t   slot;       /* MMIO slot index (0-31) */
} virtio_mmio_dev_t;

/* ═══ Transport API ═════════════════════════════════════════════ */

/* Scan MMIO slots for a device with the given ID.
 * Returns 0 on success, -1 if not found. */
int virtio_mmio_find(uint32_t device_id, virtio_mmio_dev_t *dev);

/* Initialize device: reset → ACK → DRIVER → feature negotiation → DRIVER_OK.
 * driver_features is the feature bits the driver supports (word 0 only).
 * Returns 0 on success. */
int virtio_mmio_init_device(virtio_mmio_dev_t *dev, uint32_t driver_features);

/* Set up a virtqueue. mem must point to page-aligned memory large enough
 * for 'size' descriptors. Returns 0 on success. */
int virtio_mmio_setup_vq(virtio_mmio_dev_t *dev, uint32_t queue_idx,
                         void *mem, uint16_t size, virtqueue_t *vq);

/* Allocate a descriptor from the free list. Returns index, or -1 if full. */
int vq_alloc_desc(virtqueue_t *vq);

/* Return a descriptor to the free list. */
void vq_free_desc(virtqueue_t *vq, uint16_t idx);

/* Notify device that queue has new buffers. */
void vq_kick(virtio_mmio_dev_t *dev, uint32_t queue_idx);

/* Poll for completed buffers. Returns 1 if a new used element is available,
 * writing *id and *len. Returns 0 on timeout (spins for ~timeout_ms). */
int vq_poll(virtqueue_t *vq, uint32_t *id, uint32_t *len, uint32_t timeout_ms);

/* ═══ Config Space Helpers ══════════════════════════════════════ */

static inline uint32_t virtio_mmio_config_read32(virtio_mmio_dev_t *dev, uint32_t offset) {
    return *(volatile uint32_t *)(dev->base + VIRTIO_MMIO_CONFIG + offset);
}

static inline uint16_t virtio_mmio_config_read16(virtio_mmio_dev_t *dev, uint32_t offset) {
    return *(volatile uint16_t *)(dev->base + VIRTIO_MMIO_CONFIG + offset);
}

static inline uint8_t virtio_mmio_config_read8(virtio_mmio_dev_t *dev, uint32_t offset) {
    return *(volatile uint8_t *)(dev->base + VIRTIO_MMIO_CONFIG + offset);
}

/* Signal DRIVER_OK after queue setup is complete.
 * Must be called after virtio_mmio_setup_vq(). */
void virtio_mmio_driver_ok(virtio_mmio_dev_t *dev);

/* Enable a GIC SPI interrupt for a VirtIO device */
void virtio_mmio_enable_irq(virtio_mmio_dev_t *dev);

#endif /* _KERNEL_VIRTIO_MMIO_H */
