/* CSPRNG — SHA-256 based, seeded from ARM hardware — aarch64 */
#include <kernel/crypto.h>
#include <kernel/io.h>
#include <string.h>

static uint8_t pool[SHA256_DIGEST_SIZE];
static uint32_t counter;
static int initialized;

/* Read ARM generic timer counter (CNTVCT_EL0) */
static uint64_t read_counter(void) {
    uint64_t val;
    __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
    return val;
}

void prng_init(void) {
    memset(pool, 0, sizeof(pool));
    counter = 0;

    /* Seed from ARM timer counter */
    uint64_t ctr = read_counter();
    prng_seed((const uint8_t *)&ctr, sizeof(ctr));

    /* PIT ticks */
    extern uint32_t pit_get_ticks(void);
    uint32_t ticks = pit_get_ticks();
    prng_seed((const uint8_t *)&ticks, sizeof(ticks));

    /* PL031 RTC time */
    uint32_t rtc_val = mmio_read32(0x09010000);  /* PL031 RTCDR */
    prng_seed((const uint8_t *)&rtc_val, sizeof(rtc_val));

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
        /* Mix in fresh entropy from timer counter */
        uint64_t ctr = read_counter();
        counter++;

        sha256_ctx_t ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, pool, sizeof(pool));
        sha256_update(&ctx, (const uint8_t *)&counter, sizeof(counter));
        sha256_update(&ctx, (const uint8_t *)&ctr, sizeof(ctr));

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
