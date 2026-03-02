/*
 * ac97.h — Intel AC'97 Audio Codec driver
 *
 * Supports QEMU's -device AC97 (Intel 82801AA, PCI 8086:2415).
 * Provides IRQ-driven DMA playback at 48 kHz, 16-bit signed stereo.
 */

#ifndef _KERNEL_AC97_H
#define _KERNEL_AC97_H

#include <stdint.h>

/* ── PCI identification ───────────────────────────────────────────── */

#define AC97_VENDOR_ID      0x8086
#define AC97_DEVICE_ID      0x2415

/* ── Native Audio Mixer (NAM) registers — BAR0 (I/O space) ─────── */

#define AC97_NAM_RESET          0x00
#define AC97_NAM_MASTER_VOL     0x02    /* Master volume: bits [5:0] L, [13:8] R, bit 15=mute */
#define AC97_NAM_PCM_VOL        0x18    /* PCM out volume */
#define AC97_NAM_EXT_AUDIO_ID   0x28    /* Extended Audio ID */
#define AC97_NAM_EXT_AUDIO_CTRL 0x2A    /* Extended Audio Status/Control */
#define AC97_NAM_PCM_RATE       0x2C    /* PCM front DAC sample rate */

/* ── Native Audio Bus Master (NABM) registers — BAR1 (I/O space) ─ */

/* PCM Out (PO) channel — offset 0x10 from NABM base */
#define AC97_PO_BDBAR       0x10    /* Buffer Descriptor List Base Address (32-bit) */
#define AC97_PO_CIV         0x14    /* Current Index Value (8-bit) */
#define AC97_PO_LVI         0x15    /* Last Valid Index (8-bit) */
#define AC97_PO_SR          0x16    /* Status Register (16-bit) */
#define AC97_PO_PICB        0x18    /* Position in Current Buffer (16-bit, in samples) */
#define AC97_PO_PIV         0x1A    /* Prefetched Index Value (8-bit) */
#define AC97_PO_CR          0x1B    /* Control Register (8-bit) */

/* ── Status register bits ─────────────────────────────────────────── */

#define AC97_SR_DCH         (1 << 0)    /* DMA controller halted */
#define AC97_SR_CELV        (1 << 1)    /* Current equals last valid */
#define AC97_SR_LVBCI       (1 << 2)    /* Last valid buffer completion interrupt */
#define AC97_SR_BCIS        (1 << 3)    /* Buffer completion interrupt status */
#define AC97_SR_FIFOE       (1 << 4)    /* FIFO error */

/* ── Control register bits ────────────────────────────────────────── */

#define AC97_CR_RPBM        (1 << 0)    /* Run/Pause bus master */
#define AC97_CR_RR          (1 << 1)    /* Reset registers */
#define AC97_CR_LVBIE       (1 << 2)    /* Last valid buffer interrupt enable */
#define AC97_CR_FEIE        (1 << 3)    /* FIFO error interrupt enable */
#define AC97_CR_IOCE        (1 << 4)    /* Interrupt on completion enable */

/* ── Buffer Descriptor List ───────────────────────────────────────── */

#define AC97_BDL_ENTRIES    32
#define AC97_BUF_SAMPLES    2048        /* samples per channel per buffer */

/* BDL entry: 8 bytes each */
typedef struct __attribute__((packed)) {
    uint32_t addr;          /* physical address of PCM buffer */
    uint16_t length;        /* number of samples (not bytes) */
    uint16_t flags;         /* bit 14 = BUP (buffer underrun policy), bit 15 = IOC */
} ac97_bdl_entry_t;

#define AC97_BDL_IOC        (1 << 15)   /* Interrupt on completion */
#define AC97_BDL_BUP        (1 << 14)   /* Buffer underrun policy: send last sample */

/* ── Extended Audio ID bits ───────────────────────────────────────── */

#define AC97_EA_VRA         (1 << 0)    /* Variable Rate Audio support */

/* ── Public API ───────────────────────────────────────────────────── */

int  ac97_initialize(void);             /* 0 = ok, -1 = no device */
int  ac97_is_available(void);           /* 1 if initialized successfully */
void ac97_set_master_volume(uint8_t left, uint8_t right);  /* 0=max, 63=min */
void ac97_set_pcm_volume(uint8_t left, uint8_t right);
uint32_t ac97_get_sample_rate(void);

#endif /* _KERNEL_AC97_H */
