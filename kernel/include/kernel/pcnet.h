#ifndef _KERNEL_PCNET_H
#define _KERNEL_PCNET_H

#include <stdint.h>
#include <stddef.h>

/* PCnet-FAST III (Am79C973) PCI IDs */
#define PCNET_VENDOR_ID  0x1022
#define PCNET_DEVICE_ID  0x2000

/* I/O Port Offsets (16-bit mode addresses, used for 32-bit too) */
#define PCNET_APROM     0x00  /* MAC address PROM (bytes 0x00-0x0F) */
#define PCNET_RDP       0x10  /* Register Data Port */
#define PCNET_RAP       0x14  /* Register Address Port */
#define PCNET_RESET     0x18  /* Reset register (read triggers reset) */
#define PCNET_BDP       0x1C  /* Bus Data Port (BCR access) */

/* CSR0 - Controller Status Register */
#define PCNET_CSR0_INIT  0x0001  /* Initialize */
#define PCNET_CSR0_STRT  0x0002  /* Start */
#define PCNET_CSR0_STOP  0x0004  /* Stop */
#define PCNET_CSR0_TDMD  0x0008  /* Transmit demand */
#define PCNET_CSR0_TXON  0x0010  /* Transmit on */
#define PCNET_CSR0_RXON  0x0020  /* Receive on */
#define PCNET_CSR0_IENA  0x0040  /* Interrupt enable */
#define PCNET_CSR0_INTR  0x0080  /* Interrupt flag */
#define PCNET_CSR0_IDON  0x0100  /* Initialization done */
#define PCNET_CSR0_TINT  0x0200  /* Transmit interrupt */
#define PCNET_CSR0_RINT  0x0400  /* Receive interrupt */
#define PCNET_CSR0_MERR  0x0800  /* Memory error */
#define PCNET_CSR0_MISS  0x1000  /* Missed frame */
#define PCNET_CSR0_CERR  0x2000  /* Collision error */
#define PCNET_CSR0_BABL  0x4000  /* Babble error */
#define PCNET_CSR0_ERR   0x8000  /* Error summary */

/* Descriptor count (must be power of 2) */
#define PCNET_RX_COUNT   8
#define PCNET_TX_COUNT   8
#define PCNET_LOG2_RX    3  /* log2(8) */
#define PCNET_LOG2_TX    3
#define PCNET_BUF_SIZE   1536

/* Descriptor status bits (TMD1/RMD1 upper 16 bits in SWSTYLE 2) */
#define PCNET_DESC_OWN   0x80000000  /* Owned by controller */
#define PCNET_DESC_ERR   0x40000000  /* Error */
#define PCNET_DESC_STP   0x02000000  /* Start of packet */
#define PCNET_DESC_ENP   0x01000000  /* End of packet */

/* Initialization block (SWSTYLE 2 - 32-bit) */
typedef struct {
    uint16_t mode;
    uint8_t  rlen;      /* encoded RX ring length (log2 << 4) */
    uint8_t  tlen;      /* encoded TX ring length (log2 << 4) */
    uint8_t  padr[6];   /* physical (MAC) address */
    uint16_t reserved;
    uint8_t  ladrf[8];  /* logical address filter (multicast) */
    uint32_t rdra;      /* RX descriptor ring physical address */
    uint32_t tdra;      /* TX descriptor ring physical address */
} __attribute__((packed)) pcnet_init_block_t;

/* RX/TX descriptor (SWSTYLE 2 - 32-bit, 16 bytes each) */
typedef struct {
    uint32_t addr;      /* buffer physical address */
    uint32_t status;    /* status + flags + BCNT */
    uint32_t mcnt;      /* message byte count (RX only) */
    uint32_t reserved;
} __attribute__((packed)) pcnet_descriptor_t;

/* PCnet functions */
int  pcnet_initialize(void);
int  pcnet_send_packet(const uint8_t* data, size_t len);
int  pcnet_receive_packet(uint8_t* buffer, size_t* len);
void pcnet_get_mac(uint8_t mac[6]);
int  pcnet_is_initialized(void);

#endif
