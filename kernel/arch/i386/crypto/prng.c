/* CSPRNG — SHA-256 based, seeded from hardware */
#include <kernel/crypto.h>
#include <kernel/io.h>
#include <string.h>

static uint8_t pool[SHA256_DIGEST_SIZE];
static uint32_t counter;
static int initialized;

/* Read RDTSC timestamp counter */
static uint64_t rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Read a byte from CMOS RTC */
static uint8_t cmos_read(uint8_t reg) {
    outb(0x70, reg);
    io_wait();
    return inb(0x71);
}

void prng_init(void) {
    memset(pool, 0, sizeof(pool));
    counter = 0;

    /* Seed from multiple entropy sources */
    uint64_t tsc = rdtsc();
    prng_seed((const uint8_t *)&tsc, sizeof(tsc));

    /* PIT ticks */
    extern uint32_t pit_get_ticks(void);
    uint32_t ticks = pit_get_ticks();
    prng_seed((const uint8_t *)&ticks, sizeof(ticks));

    /* RTC time */
    uint8_t rtc_data[6];
    rtc_data[0] = cmos_read(0x00); /* seconds */
    rtc_data[1] = cmos_read(0x02); /* minutes */
    rtc_data[2] = cmos_read(0x04); /* hours */
    rtc_data[3] = cmos_read(0x07); /* day */
    rtc_data[4] = cmos_read(0x08); /* month */
    rtc_data[5] = cmos_read(0x09); /* year */
    prng_seed(rtc_data, sizeof(rtc_data));

    initialized = 1;
}

void prng_seed(const uint8_t *data, size_t len) {
    sha256_ctx_t ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, pool, sizeof(pool));
    sha256_update(&ctx, data, len);
    sha256_final(&ctx, pool);
}

void prng_random(uint8_t *buf, size_t len) {
    if (!initialized)
        prng_init();

    size_t pos = 0;
    while (pos < len) {
        /* Mix in fresh entropy from TSC */
        uint64_t tsc = rdtsc();
        counter++;

        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, pool, sizeof(pool));
        sha256_update(&ctx, (const uint8_t *)&counter, sizeof(counter));
        sha256_update(&ctx, (const uint8_t *)&tsc, sizeof(tsc));

        uint8_t out[SHA256_DIGEST_SIZE];
        sha256_final(&ctx, out);

        /* Update pool from first half, output from second half */
        memcpy(pool, out, 16);

        size_t copy = len - pos;
        if (copy > 16) copy = 16;
        memcpy(buf + pos, out + 16, copy);
        pos += copy;
    }
}
