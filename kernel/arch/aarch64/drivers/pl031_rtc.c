/*
 * PL031 Real Time Clock — aarch64
 *
 * Implements the rtc.h API using PL031 at 0x09010000.
 * PL031 provides a simple Unix timestamp counter.
 */

#include <kernel/rtc.h>
#include <kernel/io.h>
#include <string.h>
#include <stdio.h>

/* PL031 register offsets */
#define PL031_BASE  0x09010000
#define PL031_RTCDR (PL031_BASE + 0x000)  /* Data Register (Unix seconds) */
#define PL031_RTCMR (PL031_BASE + 0x004)  /* Match Register */
#define PL031_RTCLR (PL031_BASE + 0x008)  /* Load Register */
#define PL031_RTCCR (PL031_BASE + 0x00C)  /* Control Register */

/* Epoch offset: seconds from 1970-01-01 to 2000-01-01 */
#define EPOCH_2000_OFFSET 946684800UL

/* ═══ Unix Timestamp Conversion ════════════════════════════════ */

static void unix_to_datetime(uint32_t ts, datetime_t *dt) {
    uint32_t days = ts / 86400;
    uint32_t rem  = ts % 86400;

    dt->hour   = rem / 3600;
    dt->minute = (rem % 3600) / 60;
    dt->second = rem % 60;

    int year = 1970;
    while (1) {
        int yday = 365;
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
            yday = 366;
        if (days < (uint32_t)yday) break;
        days -= yday;
        year++;
    }
    dt->year = year;

    static const int mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int month = 0;
    while (month < 12) {
        int md = mdays[month];
        if (month == 1 && leap) md = 29;
        if (days < (uint32_t)md) break;
        days -= md;
        month++;
    }
    dt->month = month + 1;
    dt->day   = days + 1;
}

/* ═══ Timezone Database ════════════════════════════════════════ */

#define TZ_COUNT 16
static const tz_entry_t tz_db[TZ_COUNT] = {
    { "UTC",                  "UTC",           0,       0 },
    { "Europe/London",        "London",        0,       1 },
    { "Europe/Paris",         "Paris",         3600,    1 },
    { "Europe/Berlin",        "Berlin",        3600,    1 },
    { "Europe/Madrid",        "Madrid",        3600,    1 },
    { "Europe/Rome",          "Rome",          3600,    1 },
    { "Europe/Moscow",        "Moscow",        10800,   0 },
    { "America/New_York",     "New York",      -18000,  1 },
    { "America/Chicago",      "Chicago",       -21600,  1 },
    { "America/Denver",       "Denver",        -25200,  1 },
    { "America/Los_Angeles",  "Los Angeles",   -28800,  1 },
    { "Asia/Tokyo",           "Tokyo",         32400,   0 },
    { "Asia/Shanghai",        "Shanghai",      28800,   0 },
    { "Asia/Dubai",           "Dubai",         14400,   0 },
    { "Asia/Kolkata",         "Kolkata",       19800,   0 },
    { "Australia/Sydney",     "Sydney",        36000,   1 },
};

const tz_entry_t *rtc_get_tz_db(int *count) {
    *count = TZ_COUNT;
    return tz_db;
}

/* DST helpers (find_tz, day_of_week, last_sunday, nth_sunday, is_dst_active,
 * tz_offset_seconds, datetime_to_epoch) deferred until config system is
 * ported in Phase 6+. See i386 rtc.c for reference implementation. */

/* ═══ Epoch Helpers ════════════════════════════════════════════ */

void epoch_to_datetime(uint32_t epoch, datetime_t *dt) {
    uint32_t days = epoch / 86400;
    uint32_t rem  = epoch % 86400;
    dt->hour   = rem / 3600;
    dt->minute = (rem % 3600) / 60;
    dt->second = rem % 60;

    int year = 2000;
    while (1) {
        int yd = 365;
        if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) yd = 366;
        if (days < (uint32_t)yd) break;
        days -= yd;
        year++;
    }
    dt->year = year;

    static const int md[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    int leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    int month = 0;
    while (month < 12) {
        int d = md[month];
        if (month == 1 && leap) d = 29;
        if (days < (uint32_t)d) break;
        days -= d;
        month++;
    }
    dt->month = month + 1;
    dt->day   = days + 1;
}

void rtc_format_epoch(uint32_t epoch, char *buf, int bufsize) {
    static const char *months[] = {
        "Jan","Feb","Mar","Apr","May","Jun",
        "Jul","Aug","Sep","Oct","Nov","Dec"
    };
    if (epoch == 0) {
        snprintf(buf, bufsize, "           -");
        return;
    }
    datetime_t dt;
    epoch_to_datetime(epoch, &dt);
    int mi = dt.month >= 1 && dt.month <= 12 ? dt.month - 1 : 0;
    snprintf(buf, bufsize, "%s %2d %02d:%02d",
             months[mi], dt.day, dt.hour, dt.minute);
}

/* ═══ RTC API ═════════════════════════════════════════════════ */

void rtc_read(datetime_t *dt) {
    uint32_t unix_secs = mmio_read32(PL031_RTCDR);
    unix_to_datetime(unix_secs, dt);
}

uint32_t rtc_get_epoch(void) {
    uint32_t unix_secs = mmio_read32(PL031_RTCDR);
    /* Convert Unix epoch (1970) to our epoch (2000) */
    if (unix_secs >= EPOCH_2000_OFFSET)
        return unix_secs - EPOCH_2000_OFFSET;
    return 0;
}

void rtc_init(void) {
    datetime_t dt;
    rtc_read(&dt);

    DBG("PL031 RTC: %u-%u-%u %u:%u:%u (UTC)",
        dt.year, dt.month, dt.day, dt.hour, dt.minute, dt.second);
}

int rtc_ntp_sync(void) {
    /* No network stack on aarch64 yet */
    return -1;
}
