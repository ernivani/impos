/*
 * ac97.c — Intel AC'97 Audio Codec driver
 *
 * Drives QEMU's Intel 82801AA (PCI 8086:2415) AC'97 controller.
 * Uses a 32-entry Buffer Descriptor List with IRQ-driven DMA for
 * continuous 48 kHz, 16-bit signed stereo playback.
 *
 * The IRQ handler calls mixer_render() to fill the next buffer,
 * keeping audio latency at ~42 ms per buffer.
 */

#include <kernel/ac97.h>
#include <kernel/audio_mixer.h>
#include <kernel/pci.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <string.h>

/* ── Static state ─────────────────────────────────────────────────── */

static int      ac97_available = 0;
static uint16_t nam_base;           /* BAR0: Native Audio Mixer I/O base */
static uint16_t nabm_base;         /* BAR1: Native Audio Bus Master I/O base */
static uint8_t  ac97_irq;
static uint32_t ac97_sample_rate = 48000;

/* ── DMA buffers (identity-mapped → phys == virt) ─────────────────── */

static ac97_bdl_entry_t bdl[AC97_BDL_ENTRIES] __attribute__((aligned(8)));

/*
 * 32 PCM buffers, each holding 2048 stereo frames = 4096 int16_t = 8192 bytes.
 * Total: 32 * 8192 = 256 KB in BSS.
 */
static int16_t pcm_bufs[AC97_BDL_ENTRIES][AC97_BUF_SAMPLES * 2]
    __attribute__((aligned(4)));

/* ── PIC IRQ unmask ───────────────────────────────────────────────── */

static void unmask_irq(int irq) {
    if (irq < 8) {
        outb(0x21, inb(0x21) & ~(1 << irq));
    } else {
        outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
    }
}

/* ── IRQ handler ──────────────────────────────────────────────────── */

static void ac97_irq_handler(registers_t *regs) {
    (void)regs;

    uint16_t sr = inw(nabm_base + AC97_PO_SR);

    /* Acknowledge all status bits by writing them back */
    outw(nabm_base + AC97_PO_SR, sr);

    if (sr & AC97_SR_BCIS) {
        /* Buffer completed — fill the buffer 2 ahead of CIV */
        uint8_t civ = inb(nabm_base + AC97_PO_CIV);
        uint8_t fill = (civ + 2) % AC97_BDL_ENTRIES;

        mixer_render(pcm_bufs[fill], AC97_BUF_SAMPLES);

        /* Advance LVI to keep DMA running */
        outb(nabm_base + AC97_PO_LVI, (civ + AC97_BDL_ENTRIES - 1) % AC97_BDL_ENTRIES);
    }

    if (sr & AC97_SR_FIFOE) {
        serial_printf("AC97: FIFO error\n");
    }
}

/* ── Codec reset + volume setup ───────────────────────────────────── */

static void ac97_reset_codec(void) {
    /* Write any value to NAM reset register to trigger cold reset */
    outw(nam_base + AC97_NAM_RESET, 0x42);

    /* Wait for codec to settle */
    for (volatile int i = 0; i < 100000; i++);

    /* Set master volume to max (0x0000 = 0 dB attenuation) */
    outw(nam_base + AC97_NAM_MASTER_VOL, 0x0000);

    /* Set PCM output volume to max */
    outw(nam_base + AC97_NAM_PCM_VOL, 0x0808);
}

static void ac97_setup_sample_rate(void) {
    /* Check if Variable Rate Audio (VRA) is supported */
    uint16_t ext_id = inw(nam_base + AC97_NAM_EXT_AUDIO_ID);

    if (ext_id & AC97_EA_VRA) {
        /* Enable VRA */
        uint16_t ext_ctrl = inw(nam_base + AC97_NAM_EXT_AUDIO_CTRL);
        ext_ctrl |= AC97_EA_VRA;
        outw(nam_base + AC97_NAM_EXT_AUDIO_CTRL, ext_ctrl);

        /* Set desired sample rate */
        outw(nam_base + AC97_NAM_PCM_RATE, 48000);
    }

    /* Read back actual rate */
    ac97_sample_rate = inw(nam_base + AC97_NAM_PCM_RATE);
    if (ac97_sample_rate == 0)
        ac97_sample_rate = 48000;   /* Fallback for codecs that don't report */
}

/* ── BDL + DMA setup ──────────────────────────────────────────────── */

static void ac97_setup_bdl(void) {
    /* Fill all buffers with silence and point BDL entries at them */
    for (int i = 0; i < AC97_BDL_ENTRIES; i++) {
        memset(pcm_bufs[i], 0, AC97_BUF_SAMPLES * 2 * sizeof(int16_t));
        bdl[i].addr   = (uint32_t)pcm_bufs[i];
        bdl[i].length = AC97_BUF_SAMPLES * 2;  /* Total samples (L+R) */
        bdl[i].flags  = AC97_BDL_IOC;          /* Interrupt every buffer */
    }
}

static void ac97_start_dma(void) {
    /* Reset the PCM Out DMA engine */
    outb(nabm_base + AC97_PO_CR, AC97_CR_RR);
    for (volatile int i = 0; i < 10000; i++);
    outb(nabm_base + AC97_PO_CR, 0);

    /* Set BDL base address */
    outl(nabm_base + AC97_PO_BDBAR, (uint32_t)bdl);

    /* Set Last Valid Index to wrap-around the entire ring */
    outb(nabm_base + AC97_PO_LVI, AC97_BDL_ENTRIES - 1);

    /* Start DMA with interrupts enabled */
    outb(nabm_base + AC97_PO_CR, AC97_CR_RPBM | AC97_CR_IOCE | AC97_CR_FEIE);
}

/* ── Public API ───────────────────────────────────────────────────── */

int ac97_initialize(void) {
    pci_device_t pci_dev;

    if (pci_find_device(AC97_VENDOR_ID, AC97_DEVICE_ID, &pci_dev) != 0) {
        serial_printf("AC97: no device found\n");
        return -1;
    }

    /* Extract I/O base addresses (mask off type bit) */
    nam_base  = (uint16_t)(pci_dev.bar[0] & ~0x3);
    nabm_base = (uint16_t)(pci_dev.bar[1] & ~0x3);
    ac97_irq  = pci_dev.interrupt_line;

    serial_printf("AC97: NAM=0x%x NABM=0x%x IRQ=%d\n", nam_base, nabm_base, ac97_irq);

    /* Enable PCI bus mastering + I/O space access */
    uint16_t cmd = pci_config_read_word(pci_dev.bus, pci_dev.device,
                                         pci_dev.function, PCI_COMMAND);
    cmd |= PCI_COMMAND_IO | PCI_COMMAND_MASTER;
    pci_config_write_word(pci_dev.bus, pci_dev.device,
                          pci_dev.function, PCI_COMMAND, cmd);

    /* Reset codec and configure volumes */
    ac97_reset_codec();

    /* Configure sample rate (attempt VRA for 48 kHz) */
    ac97_setup_sample_rate();

    /* Initialize the software mixer at our output rate */
    mixer_init(ac97_sample_rate);

    /* Set up the Buffer Descriptor List */
    ac97_setup_bdl();

    /* Register IRQ handler and unmask */
    irq_register_handler(ac97_irq, ac97_irq_handler);
    unmask_irq(ac97_irq);

    /* Start DMA playback */
    ac97_start_dma();

    ac97_available = 1;
    serial_printf("AC97: initialized, rate=%u, IRQ=%d\n", ac97_sample_rate, ac97_irq);

    return 0;
}

int ac97_is_available(void) {
    return ac97_available;
}

void ac97_set_master_volume(uint8_t left, uint8_t right) {
    if (!ac97_available) return;
    /* Bits [5:0] = right attenuation, [13:8] = left attenuation */
    uint16_t val = ((uint16_t)(left & 0x3F) << 8) | (right & 0x3F);
    outw(nam_base + AC97_NAM_MASTER_VOL, val);
}

void ac97_set_pcm_volume(uint8_t left, uint8_t right) {
    if (!ac97_available) return;
    uint16_t val = ((uint16_t)(left & 0x1F) << 8) | (right & 0x1F);
    outw(nam_base + AC97_NAM_PCM_VOL, val);
}

uint32_t ac97_get_sample_rate(void) {
    return ac97_sample_rate;
}
