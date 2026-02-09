#include <kernel/mouse.h>
#include <kernel/idt.h>
#include <kernel/io.h>
#include <kernel/gfx.h>

#define PS2_DATA    0x60
#define PS2_STATUS  0x64
#define PS2_CMD     0x64

static volatile int mouse_x;
static volatile int mouse_y;
static volatile uint8_t mouse_buttons;
static volatile int mouse_updated;

static int screen_w;
static int screen_h;

/* 3-byte packet accumulator */
static uint8_t mouse_packet[3];
static int mouse_cycle;

static void mouse_wait_write(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (!(inb(PS2_STATUS) & 0x02))
            return;
    }
}

static void mouse_wait_read(void) {
    int timeout = 100000;
    while (timeout-- > 0) {
        if (inb(PS2_STATUS) & 0x01)
            return;
    }
}

static void mouse_write(uint8_t data) {
    mouse_wait_write();
    outb(PS2_CMD, 0xD4);   /* Next byte goes to mouse */
    mouse_wait_write();
    outb(PS2_DATA, data);
}

static uint8_t mouse_read(void) {
    mouse_wait_read();
    return inb(PS2_DATA);
}

static void mouse_irq_handler(registers_t *regs) {
    (void)regs;
    uint8_t status = inb(PS2_STATUS);
    if (!(status & 0x20))
        return;  /* Not from mouse (bit 5 = auxiliary data) */

    uint8_t data = inb(PS2_DATA);

    switch (mouse_cycle) {
    case 0:
        /* Byte 0: must have bit 3 set (always-1 bit) */
        if (!(data & 0x08)) break;  /* Out of sync, skip */
        mouse_packet[0] = data;
        mouse_cycle = 1;
        break;
    case 1:
        mouse_packet[1] = data;
        mouse_cycle = 2;
        break;
    case 2:
        mouse_packet[2] = data;
        mouse_cycle = 0;

        /* Parse complete packet */
        mouse_buttons = mouse_packet[0] & 0x07;

        /* X movement (signed) */
        int dx = (int)mouse_packet[1];
        if (mouse_packet[0] & 0x10) dx |= 0xFFFFFF00;  /* Sign extend */

        /* Y movement (signed, inverted: PS/2 up = positive, screen down = positive) */
        int dy = (int)mouse_packet[2];
        if (mouse_packet[0] & 0x20) dy |= 0xFFFFFF00;

        /* Discard overflow */
        if (mouse_packet[0] & 0x40) dx = 0;
        if (mouse_packet[0] & 0x80) dy = 0;

        /* Update position (invert Y for screen coords) */
        mouse_x += dx;
        mouse_y -= dy;

        /* Clamp */
        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= screen_w) mouse_x = screen_w - 1;
        if (mouse_y >= screen_h) mouse_y = screen_h - 1;

        mouse_updated = 1;
        break;
    }
}

void mouse_initialize(void) {
    screen_w = gfx_is_active() ? (int)gfx_width()  : 640;
    screen_h = gfx_is_active() ? (int)gfx_height() : 480;
    mouse_x = screen_w / 2;
    mouse_y = screen_h / 2;
    mouse_buttons = 0;
    mouse_updated = 0;
    mouse_cycle = 0;

    /* Enable auxiliary mouse device */
    mouse_wait_write();
    outb(PS2_CMD, 0xA8);

    /* Read controller config */
    mouse_wait_write();
    outb(PS2_CMD, 0x20);
    uint8_t config = mouse_read();

    /* Set bit 1 (enable IRQ12), clear bit 5 (enable mouse clock) */
    config |= 0x02;
    config &= ~0x20;

    /* Write config back */
    mouse_wait_write();
    outb(PS2_CMD, 0x60);
    mouse_wait_write();
    outb(PS2_DATA, config);

    /* Set defaults */
    mouse_write(0xF6);
    mouse_read();  /* ACK */

    /* Enable data reporting */
    mouse_write(0xF4);
    mouse_read();  /* ACK */

    /* Register IRQ12 handler */
    irq_register_handler(12, mouse_irq_handler);
}

int mouse_get_x(void) {
    return mouse_x;
}

int mouse_get_y(void) {
    return mouse_y;
}

uint8_t mouse_get_buttons(void) {
    return mouse_buttons;
}

int mouse_poll(void) {
    if (mouse_updated) {
        mouse_updated = 0;
        return 1;
    }
    return 0;
}
