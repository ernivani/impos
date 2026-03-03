/*
 * VirtIO Block Device Driver — aarch64
 *
 * Implements the ata.h API using VirtIO MMIO block device.
 * Uses 3-descriptor chains: header → data → status.
 */

#include <kernel/ata.h>
#include <kernel/virtio_mmio.h>
#include <kernel/io.h>
#include <string.h>

/* VirtIO block request types */
#define VIRTIO_BLK_T_IN    0  /* Read */
#define VIRTIO_BLK_T_OUT   1  /* Write */
#define VIRTIO_BLK_T_FLUSH 4  /* Flush */

/* VirtIO block status values */
#define VIRTIO_BLK_S_OK     0
#define VIRTIO_BLK_S_IOERR  1
#define VIRTIO_BLK_S_UNSUPP 2

/* VirtIO block feature bits */
#define VIRTIO_BLK_F_SIZE_MAX  1
#define VIRTIO_BLK_F_SEG_MAX   2
#define VIRTIO_BLK_F_GEOMETRY  4
#define VIRTIO_BLK_F_RO        5
#define VIRTIO_BLK_F_BLK_SIZE  6
#define VIRTIO_BLK_F_FLUSH     9

/* Block request header */
struct virtio_blk_req {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} __attribute__((packed));

/* Static state */
static virtio_mmio_dev_t blk_dev;
static virtqueue_t blk_vq;
static uint8_t vq_mem[16384] __attribute__((aligned(4096)));
static struct virtio_blk_req req_hdr __attribute__((aligned(16)));
static uint8_t req_status __attribute__((aligned(16)));
static int blk_initialized = 0;
static uint64_t blk_capacity;  /* in 512-byte sectors */

/* ═══ ATA API Implementation ═══════════════════════════════════ */

int ata_initialize(void) {
    if (blk_initialized)
        return 0;

    /* Find VirtIO block device */
    if (virtio_mmio_find(VIRTIO_DEV_BLK, &blk_dev) != 0) {
        DBG("virtio-blk: device not found");
        return -1;
    }
    DBG("virtio-blk: found at slot %u (base=0x%x, IRQ=%u)",
        blk_dev.slot, (unsigned)blk_dev.base, blk_dev.irq);

    /* Init device — accept flush feature */
    uint32_t features = (1 << VIRTIO_BLK_F_FLUSH);
    if (virtio_mmio_init_device(&blk_dev, features) != 0) {
        DBG("virtio-blk: init failed");
        return -1;
    }

    /* Set up queue 0 (requestq) */
    if (virtio_mmio_setup_vq(&blk_dev, 0, vq_mem, 128, &blk_vq) != 0) {
        DBG("virtio-blk: queue setup failed");
        return -1;
    }

    /* Signal DRIVER_OK — device is now live */
    virtio_mmio_driver_ok(&blk_dev);

    /* Read capacity from config space:
     * offset 0x00: uint64_t capacity (in 512-byte sectors) */
    uint32_t cap_lo = virtio_mmio_config_read32(&blk_dev, 0);
    uint32_t cap_hi = virtio_mmio_config_read32(&blk_dev, 4);
    blk_capacity = ((uint64_t)cap_hi << 32) | cap_lo;

    DBG("virtio-blk: capacity=%u sectors (%u MB)",
        (unsigned)blk_capacity, (unsigned)(blk_capacity / 2048));

    blk_initialized = 1;
    return 0;
}

/* Submit a 3-descriptor chain and wait for completion */
static int blk_request(uint32_t type, uint64_t sector,
                       void *buf, uint32_t buf_len) {
    /* Allocate 3 descriptors (or 2 for flush) */
    int d0 = vq_alloc_desc(&blk_vq);
    int d2 = vq_alloc_desc(&blk_vq);
    int d1 = -1;

    if (d0 < 0 || d2 < 0)
        return -1;

    int has_data = (buf != NULL && buf_len > 0);
    if (has_data) {
        d1 = vq_alloc_desc(&blk_vq);
        if (d1 < 0) {
            vq_free_desc(&blk_vq, d0);
            vq_free_desc(&blk_vq, d2);
            return -1;
        }
    }

    /* Prepare header */
    req_hdr.type = type;
    req_hdr.reserved = 0;
    req_hdr.sector = sector;
    req_status = 0xFF;

    /* Descriptor 0: header (device-readable) */
    blk_vq.desc[d0].addr = (uint64_t)(uintptr_t)&req_hdr;
    blk_vq.desc[d0].len = sizeof(req_hdr);
    blk_vq.desc[d0].flags = VRING_DESC_F_NEXT;

    if (has_data) {
        blk_vq.desc[d0].next = (uint16_t)d1;

        /* Descriptor 1: data buffer */
        blk_vq.desc[d1].addr = (uint64_t)(uintptr_t)buf;
        blk_vq.desc[d1].len = buf_len;
        blk_vq.desc[d1].flags = VRING_DESC_F_NEXT;
        if (type == VIRTIO_BLK_T_IN)
            blk_vq.desc[d1].flags |= VRING_DESC_F_WRITE;  /* device writes to buf */
        blk_vq.desc[d1].next = (uint16_t)d2;
    } else {
        blk_vq.desc[d0].next = (uint16_t)d2;
    }

    /* Last descriptor: status byte (device-writable) */
    blk_vq.desc[d2].addr = (uint64_t)(uintptr_t)&req_status;
    blk_vq.desc[d2].len = 1;
    blk_vq.desc[d2].flags = VRING_DESC_F_WRITE;
    blk_vq.desc[d2].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = blk_vq.avail->idx % blk_vq.size;
    blk_vq.avail->ring[avail_idx] = (uint16_t)d0;
    __asm__ volatile("dmb ish" ::: "memory");
    blk_vq.avail->idx++;

    /* Kick device */
    vq_kick(&blk_dev, 0);

    /* Wait for completion */
    uint32_t id, len;
    if (!vq_poll(&blk_vq, &id, &len, 5000)) {
        DBG("virtio-blk: request timeout (type=%u, sector=%u)", type, (unsigned)sector);
        return -1;
    }

    /* Free descriptors */
    if (has_data) vq_free_desc(&blk_vq, d1);
    vq_free_desc(&blk_vq, d0);
    vq_free_desc(&blk_vq, d2);

    return (req_status == VIRTIO_BLK_S_OK) ? 0 : -1;
}

int ata_read_sectors(uint32_t lba, uint8_t sector_count, uint8_t *buffer) {
    if (!blk_initialized)
        return -1;

    /* Process one sector at a time (simplest, most reliable) */
    for (uint8_t i = 0; i < sector_count; i++) {
        int rc = blk_request(VIRTIO_BLK_T_IN, lba + i,
                             buffer + i * ATA_SECTOR_SIZE, ATA_SECTOR_SIZE);
        if (rc != 0)
            return -1;
    }
    return 0;
}

int ata_write_sectors(uint32_t lba, uint8_t sector_count, const uint8_t *buffer) {
    if (!blk_initialized)
        return -1;

    for (uint8_t i = 0; i < sector_count; i++) {
        int rc = blk_request(VIRTIO_BLK_T_OUT, lba + i,
                             (void *)(buffer + i * ATA_SECTOR_SIZE), ATA_SECTOR_SIZE);
        if (rc != 0)
            return -1;
    }
    return 0;
}

int ata_flush(void) {
    if (!blk_initialized)
        return -1;
    return blk_request(VIRTIO_BLK_T_FLUSH, 0, NULL, 0);
}

int ata_is_available(void) {
    return blk_initialized;
}
