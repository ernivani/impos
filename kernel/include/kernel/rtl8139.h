#ifndef _KERNEL_RTL8139_H
#define _KERNEL_RTL8139_H

#include <stdint.h>
#include <stddef.h>

/* RTL8139 Vendor/Device IDs */
#define RTL8139_VENDOR_ID 0x10EC
#define RTL8139_DEVICE_ID 0x8139

/* RTL8139 Registers */
#define RTL8139_IDR0      0x00  /* MAC address */
#define RTL8139_MAR0      0x08  /* Multicast filter */
#define RTL8139_TXSTATUS0 0x10  /* Transmit status (4 32bit registers) */
#define RTL8139_TXADDR0   0x20  /* Tx descriptors (also 4 32bit) */
#define RTL8139_RXBUF     0x30  /* Receive buffer start address */
#define RTL8139_RXEARLYCNT 0x34 /* Early Rx byte count */
#define RTL8139_RXEARLYSTATUS 0x36 /* Early Rx status */
#define RTL8139_CHIPCMD   0x37  /* Command register */
#define RTL8139_RXBUFTAIL 0x38  /* Current address of packet read (queue tail) */
#define RTL8139_RXBUFHEAD 0x3A  /* Current buffer address (queue head) */
#define RTL8139_INTRMASK  0x3C  /* Interrupt mask */
#define RTL8139_INTRSTATUS 0x3E /* Interrupt status */
#define RTL8139_TXCONFIG  0x40  /* Tx config */
#define RTL8139_RXCONFIG  0x44  /* Rx config */
#define RTL8139_TIMER     0x48  /* A general purpose counter */
#define RTL8139_RXMISSED  0x4C  /* 24 bits valid, write clears */
#define RTL8139_CFG9346   0x50  /* 93C46 command register */
#define RTL8139_CONFIG0   0x51  /* Configuration reg 0 */
#define RTL8139_CONFIG1   0x52  /* Configuration reg 1 */
#define RTL8139_TIMERINT  0x54  /* Timer interrupt register (32 bits) */
#define RTL8139_MEDIASTATUS 0x58 /* Media status register */
#define RTL8139_CONFIG3   0x59  /* Config register 3 */
#define RTL8139_CONFIG4   0x5A  /* Config register 4 */
#define RTL8139_MULINT    0x5C  /* Multiple interrupt select */
#define RTL8139_RERID     0x5E  /* PCI Revision ID */
#define RTL8139_TSAD      0x60  /* Transmit status of all descriptors (16 bits) */
#define RTL8139_BMCR      0x62  /* Basic Mode Control Register (16 bits) */
#define RTL8139_BMSR      0x64  /* Basic Mode Status Register (16 bits) */
#define RTL8139_ANAR      0x66  /* Auto-Negotiation Advertisement Register (16 bits) */
#define RTL8139_ANLPAR    0x68  /* Auto-Negotiation Link Partner Register (16 bits) */
#define RTL8139_ANER      0x6A  /* Auto-Negotiation Expansion Register (16 bits) */
#define RTL8139_DIS       0x6C  /* Disconnect counter (16 bits) */
#define RTL8139_FCSC      0x6E  /* False Carrier Sense Counter (16 bits) */
#define RTL8139_NWAYTR    0x70  /* N-way Test Register (16 bits) */
#define RTL8139_REC       0x72  /* RX_ER Counter (16 bits) */
#define RTL8139_CSCR      0x74  /* CS Configuration Register (16 bits) */
#define RTL8139_PHY1_PARM 0x78  /* PHY parameter 1 */
#define RTL8139_TW_PARM   0x7C  /* Twister parameter */
#define RTL8139_PHY2_PARM 0x80  /* PHY parameter 2 */

/* Command register bits */
#define RTL8139_CMD_RESET   0x10
#define RTL8139_CMD_RX_ENABLE 0x08
#define RTL8139_CMD_TX_ENABLE 0x04
#define RTL8139_CMD_BUF_EMPTY 0x01

/* Interrupt status bits */
#define RTL8139_INT_PCIERR     0x8000  /* PCI Bus error */
#define RTL8139_INT_TIMEOUT    0x4000  /* PCS Timeout */
#define RTL8139_INT_RXFIFO_OVERFLOW 0x0040 /* Rx FIFO overflow */
#define RTL8139_INT_RXFIFO_UNDERRUN 0x0020 /* Packet underrun / link change */
#define RTL8139_INT_LINK_CHANGE 0x0020
#define RTL8139_INT_RXBUF_OVERFLOW 0x0010 /* Rx BUFFER overflow */
#define RTL8139_INT_TX_ERR     0x0008
#define RTL8139_INT_TX_OK      0x0004
#define RTL8139_INT_RX_ERR     0x0002
#define RTL8139_INT_RX_OK      0x0001

/* Transmit status bits */
#define RTL8139_TX_HOST_OWNS   0x00002000
#define RTL8139_TX_UNDERRUN    0x00004000
#define RTL8139_TX_STATUS_OK   0x00008000
#define RTL8139_TX_OUT_OF_WINDOW 0x20000000
#define RTL8139_TX_ABORTED     0x40000000
#define RTL8139_TX_CARRIER_LOST 0x80000000

/* Receive config bits */
#define RTL8139_RX_CONFIG_ACCEPT_ERROR 0x00000020
#define RTL8139_RX_CONFIG_ACCEPT_RUNT  0x00000010
#define RTL8139_RX_CONFIG_ACCEPT_BROADCAST 0x00000008
#define RTL8139_RX_CONFIG_ACCEPT_MULTICAST 0x00000004
#define RTL8139_RX_CONFIG_ACCEPT_MATCH 0x00000002
#define RTL8139_RX_CONFIG_ACCEPT_ALL_PHYS 0x00000001
#define RTL8139_RX_CONFIG_WRAP         0x00000080
#define RTL8139_RX_CONFIG_8K_BUFFER    0x00000000
#define RTL8139_RX_CONFIG_16K_BUFFER   0x00000800
#define RTL8139_RX_CONFIG_32K_BUFFER   0x00001000
#define RTL8139_RX_CONFIG_64K_BUFFER   0x00001800

/* Transmit config bits */
#define RTL8139_TX_CONFIG_IFG96        0x03000000
#define RTL8139_TX_CONFIG_LOOPBACK     0x00060000

/* 93C46 Command bits */
#define RTL8139_CFG9346_LOCK   0x00
#define RTL8139_CFG9346_UNLOCK 0xC0

/* Buffer sizes */
#define RTL8139_RX_BUFFER_SIZE (8192 + 16 + 1536)
#define RTL8139_TX_BUFFER_SIZE 1536
#define RTL8139_NUM_TX_DESC 4

/* RTL8139 Device Structure */
typedef struct {
    uint32_t io_base;
    uint8_t irq;
    uint8_t mac[6];
    
    /* RX */
    uint8_t* rx_buffer;
    uint32_t rx_buffer_phys;
    uint16_t rx_offset;
    
    /* TX */
    uint8_t* tx_buffer[RTL8139_NUM_TX_DESC];
    uint32_t tx_buffer_phys[RTL8139_NUM_TX_DESC];
    uint8_t tx_current;
    
    int initialized;
} rtl8139_device_t;

/* RTL8139 Functions */
int rtl8139_initialize(void);
int rtl8139_send_packet(const uint8_t* data, size_t len);
int rtl8139_receive_packet(uint8_t* buffer, size_t* len);
void rtl8139_get_mac(uint8_t mac[6]);
int rtl8139_is_initialized(void);

#endif
