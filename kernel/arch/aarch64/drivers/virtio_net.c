/*
 * VirtIO Network Device Driver — aarch64
 *
 * Standalone network driver using VirtIO MMIO transport.
 * Provides TX/RX packet interface for future net stack (Phase 8).
 */

#include <kernel/virtio_mmio.h>
#include <kernel/io.h>
#include <string.h>

/* VirtIO net feature bits */
#define VIRTIO_NET_F_MAC          (1 << 5)
#define VIRTIO_NET_F_STATUS       (1 << 16)
#define VIRTIO_NET_F_MRG_RXBUF   (1 << 15)

/* VirtIO net header (no offload) */
struct virtio_net_hdr {
    uint8_t  flags;
    uint8_t  gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /* num_buffers only present if VIRTIO_NET_F_MRG_RXBUF */
} __attribute__((packed));

#define NET_HDR_SIZE sizeof(struct virtio_net_hdr)

/* Queue indices */
#define RX_QUEUE 0
#define TX_QUEUE 1

/* Buffer sizes */
#define MTU         1514
#define RX_BUF_SIZE (MTU + NET_HDR_SIZE)
#define NUM_RX_BUFS 16
#define VQ_SIZE     64

/* Static state */
static virtio_mmio_dev_t net_dev;
static virtqueue_t rx_vq, tx_vq;
static uint8_t rx_vq_mem[16384] __attribute__((aligned(4096)));
static uint8_t tx_vq_mem[16384] __attribute__((aligned(4096)));

/* RX buffers pre-posted to device */
static uint8_t rx_bufs[NUM_RX_BUFS][RX_BUF_SIZE] __attribute__((aligned(16)));
static uint16_t rx_desc_ids[NUM_RX_BUFS];

static uint8_t net_mac[6];
static int net_initialized = 0;

/* TX header (reused for all sends) */
static struct virtio_net_hdr tx_hdr __attribute__((aligned(16)));

/* ═══ RX Buffer Management ════════════════════════════════════ */

static void post_rx_buffers(void) {
    for (int i = 0; i < NUM_RX_BUFS; i++) {
        int desc = vq_alloc_desc(&rx_vq);
        if (desc < 0) break;

        rx_desc_ids[i] = (uint16_t)desc;
        rx_vq.desc[desc].addr = (uint64_t)(uintptr_t)rx_bufs[i];
        rx_vq.desc[desc].len = RX_BUF_SIZE;
        rx_vq.desc[desc].flags = VRING_DESC_F_WRITE;
        rx_vq.desc[desc].next = 0;

        uint16_t avail_idx = rx_vq.avail->idx % rx_vq.size;
        rx_vq.avail->ring[avail_idx] = (uint16_t)desc;
        __asm__ volatile("dmb ish" ::: "memory");
        rx_vq.avail->idx++;
    }
    vq_kick(&net_dev, RX_QUEUE);
}

/* ═══ Public API ══════════════════════════════════════════════ */

int virtio_net_initialize(void) {
    if (net_initialized)
        return 0;

    if (virtio_mmio_find(VIRTIO_DEV_NET, &net_dev) != 0) {
        DBG("virtio-net: device not found");
        return -1;
    }
    DBG("virtio-net: found at slot %u (base=0x%x, IRQ=%u)",
        net_dev.slot, (unsigned)net_dev.base, net_dev.irq);

    /* Negotiate MAC feature */
    uint32_t features = VIRTIO_NET_F_MAC;
    if (virtio_mmio_init_device(&net_dev, features) != 0) {
        DBG("virtio-net: init failed");
        return -1;
    }

    /* Read MAC from config space (offsets 0x00-0x05) */
    for (int i = 0; i < 6; i++)
        net_mac[i] = virtio_mmio_config_read8(&net_dev, i);

    DBG("virtio-net: MAC=%x:%x:%x:%x:%x:%x",
        net_mac[0], net_mac[1], net_mac[2],
        net_mac[3], net_mac[4], net_mac[5]);

    /* Set up RX queue */
    if (virtio_mmio_setup_vq(&net_dev, RX_QUEUE, rx_vq_mem, VQ_SIZE, &rx_vq) != 0) {
        DBG("virtio-net: RX queue setup failed");
        return -1;
    }

    /* Set up TX queue */
    if (virtio_mmio_setup_vq(&net_dev, TX_QUEUE, tx_vq_mem, VQ_SIZE, &tx_vq) != 0) {
        DBG("virtio-net: TX queue setup failed");
        return -1;
    }

    /* Signal DRIVER_OK — device is now live */
    virtio_mmio_driver_ok(&net_dev);

    /* Pre-post RX buffers */
    post_rx_buffers();

    /* Zero TX header (no offload) */
    memset(&tx_hdr, 0, sizeof(tx_hdr));

    net_initialized = 1;
    return 0;
}

int virtio_net_send_packet(const uint8_t *data, uint32_t len) {
    if (!net_initialized || len > MTU)
        return -1;

    /* Allocate 2 descriptors: header + data */
    int d0 = vq_alloc_desc(&tx_vq);
    int d1 = vq_alloc_desc(&tx_vq);
    if (d0 < 0 || d1 < 0) {
        if (d0 >= 0) vq_free_desc(&tx_vq, d0);
        return -1;
    }

    /* Descriptor 0: net header (device-readable) */
    tx_vq.desc[d0].addr = (uint64_t)(uintptr_t)&tx_hdr;
    tx_vq.desc[d0].len = NET_HDR_SIZE;
    tx_vq.desc[d0].flags = VRING_DESC_F_NEXT;
    tx_vq.desc[d0].next = (uint16_t)d1;

    /* Descriptor 1: packet data (device-readable) */
    tx_vq.desc[d1].addr = (uint64_t)(uintptr_t)data;
    tx_vq.desc[d1].len = len;
    tx_vq.desc[d1].flags = 0;
    tx_vq.desc[d1].next = 0;

    /* Add to available ring */
    uint16_t avail_idx = tx_vq.avail->idx % tx_vq.size;
    tx_vq.avail->ring[avail_idx] = (uint16_t)d0;
    __asm__ volatile("dmb ish" ::: "memory");
    tx_vq.avail->idx++;

    vq_kick(&net_dev, TX_QUEUE);

    /* Wait for TX completion */
    uint32_t id, used_len;
    if (!vq_poll(&tx_vq, &id, &used_len, 1000)) {
        return -1;
    }

    vq_free_desc(&tx_vq, d0);
    vq_free_desc(&tx_vq, d1);
    return 0;
}

int virtio_net_receive_packet(uint8_t *buf, uint32_t buf_size, uint32_t *out_len) {
    if (!net_initialized)
        return -1;

    uint32_t id, len;
    if (!vq_poll(&rx_vq, &id, &len, 100))
        return -1;  /* no packet available */

    /* Copy received data (skip virtio_net_hdr) */
    if (len > NET_HDR_SIZE) {
        uint32_t pkt_len = len - NET_HDR_SIZE;
        if (pkt_len > buf_size) pkt_len = buf_size;
        uint8_t *rx_data = rx_bufs[id % NUM_RX_BUFS] + NET_HDR_SIZE;
        memcpy(buf, rx_data, pkt_len);
        if (out_len) *out_len = pkt_len;
    } else {
        if (out_len) *out_len = 0;
    }

    /* Re-post the buffer */
    uint16_t desc = (uint16_t)id;
    rx_vq.desc[desc].addr = (uint64_t)(uintptr_t)rx_bufs[id % NUM_RX_BUFS];
    rx_vq.desc[desc].len = RX_BUF_SIZE;
    rx_vq.desc[desc].flags = VRING_DESC_F_WRITE;

    uint16_t avail_idx = rx_vq.avail->idx % rx_vq.size;
    rx_vq.avail->ring[avail_idx] = desc;
    __asm__ volatile("dmb ish" ::: "memory");
    rx_vq.avail->idx++;
    vq_kick(&net_dev, RX_QUEUE);

    return 0;
}

void virtio_net_get_mac(uint8_t *mac) {
    memcpy(mac, net_mac, 6);
}
