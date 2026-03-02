#ifndef _KERNEL_UHCI_H
#define _KERNEL_UHCI_H

#include <stdint.h>

/* ── UHCI Registers (I/O space offsets from BAR4) ────────────────── */
#define UHCI_USBCMD     0x00    /* USB Command */
#define UHCI_USBSTS     0x02    /* USB Status */
#define UHCI_USBINTR    0x04    /* USB Interrupt Enable */
#define UHCI_FRNUM      0x06    /* Frame Number */
#define UHCI_FLBASEADD  0x08    /* Frame List Base Address (32-bit) */
#define UHCI_SOFMOD     0x0C    /* Start of Frame Modify */
#define UHCI_PORTSC1    0x10    /* Port 1 Status/Control */
#define UHCI_PORTSC2    0x12    /* Port 2 Status/Control */

/* USBCMD bits */
#define UHCI_CMD_RS         0x0001  /* Run/Stop */
#define UHCI_CMD_HCRESET    0x0002  /* Host Controller Reset */
#define UHCI_CMD_GRESET     0x0004  /* Global Reset */
#define UHCI_CMD_MAXP       0x0080  /* Max Packet (64 bytes) */

/* USBSTS bits */
#define UHCI_STS_USBINT     0x0001
#define UHCI_STS_ERROR      0x0002
#define UHCI_STS_RESUME     0x0004
#define UHCI_STS_HSE        0x0008  /* Host System Error */
#define UHCI_STS_HCPE       0x0010  /* Host Controller Process Error */
#define UHCI_STS_HCH        0x0020  /* HC Halted */

/* PORTSC bits */
#define UHCI_PORT_CCS       0x0001  /* Current Connect Status */
#define UHCI_PORT_CSC       0x0002  /* Connect Status Change */
#define UHCI_PORT_PE        0x0004  /* Port Enable */
#define UHCI_PORT_PEC       0x0008  /* Port Enable Change */
#define UHCI_PORT_LSDA      0x0100  /* Low Speed Device Attached */
#define UHCI_PORT_RESET     0x0200  /* Port Reset */
#define UHCI_PORT_SUSP      0x1000  /* Suspend */

/* TD link pointer bits */
#define UHCI_LP_TERMINATE   0x0001  /* Terminate (invalid pointer) */
#define UHCI_LP_QH          0x0002  /* Points to QH (not TD) */
#define UHCI_LP_DEPTH       0x0004  /* Depth-first traversal */

/* TD status bits */
#define UHCI_TD_ACTIVE      (1 << 23)
#define UHCI_TD_STALLED     (1 << 22)
#define UHCI_TD_DATABUF     (1 << 21)
#define UHCI_TD_BABBLE      (1 << 20)
#define UHCI_TD_NAK         (1 << 19)
#define UHCI_TD_CRCTMO      (1 << 18)
#define UHCI_TD_BITSTUFF    (1 << 17)

/* TD PID values */
#define UHCI_PID_SETUP      0x2D
#define UHCI_PID_IN         0x69
#define UHCI_PID_OUT        0xE1

/* USB standard requests */
#define USB_REQ_GET_DESCRIPTOR  0x06
#define USB_DESC_DEVICE         0x01
#define USB_DESC_STRING         0x03

/* ── Data Structures ─────────────────────────────────────────────── */

/* Transfer Descriptor (16 bytes, 16-byte aligned) */
typedef struct {
    uint32_t link;      /* Link to next TD/QH */
    uint32_t status;    /* Control and status */
    uint32_t token;     /* PID, device addr, endpoint, toggle, maxlen */
    uint32_t buffer;    /* Data buffer physical address */
} __attribute__((aligned(16))) uhci_td_t;

/* Queue Head (8 bytes + padding for alignment) */
typedef struct {
    uint32_t head;      /* Horizontal link → next QH */
    uint32_t element;   /* Vertical link → first TD */
    uint32_t _pad[2];   /* Pad to 16 bytes for alignment */
} __attribute__((aligned(16))) uhci_qh_t;

/* USB Device Descriptor (18 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* ── Public API ──────────────────────────────────────────────────── */

void uhci_initialize(void);
int  uhci_get_device_count(void);
int  uhci_get_device_info(int idx, uint16_t *vendor, uint16_t *product,
                          uint8_t *dev_class, uint8_t *dev_subclass);

#endif
