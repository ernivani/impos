#include <kernel/beep.h>
#include <kernel/io.h>
#include <kernel/idt.h>

/* PIT channel 2 for PC speaker */
#define PIT_CHANNEL2  0x42
#define PIT_CMD       0x43
#define PIT_FREQ      1193182
#define SPEAKER_PORT  0x61

static void speaker_on(uint32_t freq) {
    if (freq == 0) return;
    uint16_t divisor = (uint16_t)(PIT_FREQ / freq);

    /* Set PIT channel 2 to mode 3 (square wave) */
    outb(PIT_CMD, 0xB6);  /* Channel 2, lobyte/hibyte, mode 3 */
    outb(PIT_CHANNEL2, divisor & 0xFF);
    outb(PIT_CHANNEL2, (divisor >> 8) & 0xFF);

    /* Enable speaker (bits 0 and 1 of port 0x61) */
    uint8_t val = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, val | 0x03);
}

static void speaker_off(void) {
    uint8_t val = inb(SPEAKER_PORT);
    outb(SPEAKER_PORT, val & ~0x03);
}

void beep(uint32_t freq, uint32_t duration_ms) {
    speaker_on(freq);
    pit_sleep_ms(duration_ms);
    speaker_off();
}

void beep_ok(void) {
    beep(880, 80);
}

void beep_error(void) {
    beep(220, 100);
    pit_sleep_ms(50);
    beep(220, 100);
}

void beep_notify(void) {
    beep(660, 60);
    pit_sleep_ms(30);
    beep(880, 80);
}

void beep_startup(void) {
    beep(523, 80);   /* C5 */
    pit_sleep_ms(20);
    beep(659, 80);   /* E5 */
    pit_sleep_ms(20);
    beep(784, 100);  /* G5 */
}
