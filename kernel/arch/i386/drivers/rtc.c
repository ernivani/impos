#include <kernel/rtc.h>
#include <kernel/io.h>
#include <kernel/config.h>
#include <kernel/udp.h>
#include <kernel/dns.h>
#include <kernel/net.h>
#include <string.h>
#include <stdio.h>

/* CMOS RTC ports */
#define CMOS_ADDR  0x70
#define CMOS_DATA  0x71

/* CMOS registers */
#define RTC_SECONDS   0x00
#define RTC_MINUTES   0x02
#define RTC_HOURS     0x04
#define RTC_DAY       0x07
#define RTC_MONTH     0x08
#define RTC_YEAR      0x09
#define RTC_STATUS_A  0x0A
#define RTC_STATUS_B  0x0B

/* NTP */
#define NTP_PORT       123
#define NTP_LOCAL_PORT 12300
#define NTP_EPOCH_OFFSET 2208988800UL  /* seconds from 1900 to 1970 */

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    io_wait();
    return inb(CMOS_DATA);
}

static int rtc_is_updating(void) {
    return cmos_read(RTC_STATUS_A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd & 0x0F) + ((bcd >> 4) * 10);
}

void rtc_read(datetime_t *dt) {
    /* Wait until RTC is not updating */
    while (rtc_is_updating());

    uint8_t sec  = cmos_read(RTC_SECONDS);
    uint8_t min  = cmos_read(RTC_MINUTES);
    uint8_t hour = cmos_read(RTC_HOURS);
    uint8_t day  = cmos_read(RTC_DAY);
    uint8_t mon  = cmos_read(RTC_MONTH);
    uint8_t year = cmos_read(RTC_YEAR);

    /* Read a second time to avoid inconsistency during update */
    uint8_t sec2, min2, hour2, day2, mon2, year2;
    do {
        sec2  = sec;  min2  = min;  hour2 = hour;
        day2  = day;  mon2  = mon;  year2 = year;

        while (rtc_is_updating());
        sec  = cmos_read(RTC_SECONDS);
        min  = cmos_read(RTC_MINUTES);
        hour = cmos_read(RTC_HOURS);
        day  = cmos_read(RTC_DAY);
        mon  = cmos_read(RTC_MONTH);
        year = cmos_read(RTC_YEAR);
    } while (sec != sec2 || min != min2 || hour != hour2 ||
             day != day2 || mon != mon2 || year != year2);

    /* Check if BCD mode (bit 2 of status B = 0 means BCD) */
    uint8_t status_b = cmos_read(RTC_STATUS_B);
    int is_bcd = !(status_b & 0x04);
    int is_12h = !(status_b & 0x02);
    int pm = hour & 0x80;

    if (is_bcd) {
        sec  = bcd_to_bin(sec);
        min  = bcd_to_bin(min);
        hour = bcd_to_bin(hour & 0x7F);
        day  = bcd_to_bin(day);
        mon  = bcd_to_bin(mon);
        year = bcd_to_bin(year);
    } else {
        hour = hour & 0x7F;
    }

    /* Convert 12h to 24h if needed */
    if (is_12h && pm) {
        hour = (hour % 12) + 12;
    } else if (is_12h && !pm && hour == 12) {
        hour = 0;
    }

    dt->second = sec;
    dt->minute = min;
    dt->hour   = hour;
    dt->day    = day;
    dt->month  = mon;
    dt->year   = 2000 + year;  /* CMOS year is 0-99 */
}

/* ═══ Timezone database ═══════════════════════════════════════ */

/* EU DST: last Sunday of March to last Sunday of October
   US DST: 2nd Sunday of March to 1st Sunday of November
   No DST: Asia/Tokyo, Asia/Shanghai, etc. */
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

static const tz_entry_t *find_tz(const char *name) {
    for (int i = 0; i < TZ_COUNT; i++)
        if (strcmp(tz_db[i].name, name) == 0)
            return &tz_db[i];
    return &tz_db[0]; /* UTC fallback */
}

/* Day of week: 0=Sun, 1=Mon, ... 6=Sat (Tomohiko Sakamoto's algorithm) */
static int day_of_week(int y, int m, int d) {
    static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
}

/* Find last Sunday of a given month in a given year (returns day 1-31) */
static int last_sunday(int year, int month) {
    /* Days in month */
    static const int mdays[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    int dim = mdays[month];
    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0))
        dim = 29;
    int dow = day_of_week(year, month, dim); /* dow of last day */
    return dim - dow; /* back up to Sunday */
}

/* Find nth Sunday of a given month (n=1 for 1st, n=2 for 2nd) */
static int nth_sunday(int year, int month, int n) {
    int dow1 = day_of_week(year, month, 1); /* dow of 1st */
    int first_sun = (dow1 == 0) ? 1 : (8 - dow1);
    return first_sun + (n - 1) * 7;
}

/* Check if UTC datetime falls within DST for a given timezone */
static int is_dst_active(const datetime_t *utc, const tz_entry_t *tz) {
    if (!tz->has_dst) return 0;

    system_config_t *cfg = config_get();
    if (!cfg->auto_dst) return 0;

    int y = utc->year;

    /* EU DST: last Sun March 01:00 UTC to last Sun October 01:00 UTC */
    if (tz->std_offset >= 0 && tz->std_offset <= 3600 * 3) {
        int mar_sun = last_sunday(y, 3);
        int oct_sun = last_sunday(y, 10);

        /* Build epoch-style comparison (month*100+day)*100+hour */
        int now_val  = (utc->month * 100 + utc->day) * 100 + utc->hour;
        int dst_start = (3 * 100 + mar_sun) * 100 + 1;  /* March last Sun 01:00 UTC */
        int dst_end   = (10 * 100 + oct_sun) * 100 + 1;  /* October last Sun 01:00 UTC */

        return (now_val >= dst_start && now_val < dst_end);
    }

    /* US DST: 2nd Sun March 02:00 local to 1st Sun November 02:00 local */
    if (tz->std_offset < 0) {
        int mar_sun = nth_sunday(y, 3, 2);
        int nov_sun = nth_sunday(y, 11, 1);

        /* Approximate: compare in UTC by adjusting transition times */
        int std_h = -tz->std_offset / 3600;
        int now_val   = (utc->month * 100 + utc->day) * 100 + utc->hour;
        int dst_start = (3 * 100 + mar_sun) * 100 + (2 + std_h);
        int dst_end   = (11 * 100 + nov_sun) * 100 + (2 + std_h - 1); /* -1 for DST offset */

        return (now_val >= dst_start && now_val < dst_end);
    }

    /* Australia DST: 1st Sun October to 1st Sun April (southern hemisphere) */
    if (tz->std_offset >= 36000) {
        int oct_sun = nth_sunday(y, 10, 1);
        int apr_sun = nth_sunday(y, 4, 1);

        int now_val   = (utc->month * 100 + utc->day) * 100 + utc->hour;
        int dst_start = (10 * 100 + oct_sun) * 100 + 2;
        int dst_end   = (4 * 100 + apr_sun) * 100 + 3;

        /* Southern hemisphere: DST is Oct-Apr (wraps around new year) */
        return (now_val >= dst_start || now_val < dst_end);
    }

    return 0;
}

/* Get timezone offset in seconds from config (including DST if auto) */
static int tz_offset_seconds(void) {
    const char *tz_name = config_get_timezone();
    const tz_entry_t *tz = find_tz(tz_name);

    int offset = tz->std_offset;

    /* Check DST: we need current UTC time to decide */
    datetime_t utc;
    rtc_read(&utc);  /* CMOS is always UTC */

    if (is_dst_active(&utc, tz))
        offset += 3600;  /* DST = +1 hour */

    return offset;
}

/* Convert a datetime to a simple epoch (seconds since 2000-01-01 00:00 UTC) */
static uint32_t datetime_to_epoch(const datetime_t *dt) {
    uint32_t days = 0;
    for (int y = 2000; y < dt->year; y++) {
        days += 365;
        if ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0) days++;
    }
    static const int md[] = {0,31,28,31,30,31,30,31,31,30,31,30,31};
    for (int m = 1; m < dt->month && m <= 12; m++) {
        days += md[m];
        if (m == 2 && ((dt->year % 4 == 0 && dt->year % 100 != 0) || dt->year % 400 == 0))
            days++;
    }
    days += dt->day - 1;
    return days * 86400 + dt->hour * 3600 + dt->minute * 60 + dt->second;
}

/* Convert epoch (seconds since 2000-01-01) back to datetime */
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

void rtc_init(void) {
    /* Seed system clock from CMOS hardware (CMOS is UTC) */
    datetime_t dt;
    rtc_read(&dt);

    /* Apply timezone offset */
    uint32_t epoch = datetime_to_epoch(&dt) + tz_offset_seconds();
    epoch_to_datetime(epoch, &dt);

    /* Set without saving to disk (just populate in-memory config) */
    system_config_t *cfg = config_get();
    memcpy(&cfg->datetime, &dt, sizeof(datetime_t));
}

/* ═══ NTP time sync ═══════════════════════════════════════════ */

/* NTP packet: 48 bytes minimum */
typedef struct {
    uint8_t  li_vn_mode;      /* LI(2) | VN(3) | Mode(3) */
    uint8_t  stratum;
    uint8_t  poll;
    int8_t   precision;
    uint32_t root_delay;
    uint32_t root_dispersion;
    uint32_t ref_id;
    uint32_t ref_ts_sec;
    uint32_t ref_ts_frac;
    uint32_t orig_ts_sec;
    uint32_t orig_ts_frac;
    uint32_t rx_ts_sec;
    uint32_t rx_ts_frac;
    uint32_t tx_ts_sec;
    uint32_t tx_ts_frac;
} __attribute__((packed)) ntp_packet_t;

static uint32_t ntohl(uint32_t n) {
    return ((n & 0xFF) << 24) | ((n & 0xFF00) << 8) |
           ((n >> 8) & 0xFF00) | ((n >> 24) & 0xFF);
}

/* Convert Unix timestamp to datetime */
static void unix_to_datetime(uint32_t ts, datetime_t *dt) {
    /* Days from 1970-01-01 */
    uint32_t days = ts / 86400;
    uint32_t rem  = ts % 86400;

    dt->hour   = rem / 3600;
    dt->minute = (rem % 3600) / 60;
    dt->second = rem % 60;

    /* Calculate year */
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

    /* Calculate month */
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

int rtc_ntp_sync(void) {
    net_config_t *ncfg = net_get_config();
    if (!ncfg || !ncfg->link_up) return -1;

    /* Resolve pool.ntp.org */
    uint8_t ntp_ip[4];
    if (dns_resolve("pool.ntp.org", ntp_ip) != 0)
        return -1;

    /* Bind local port for response */
    if (udp_bind(NTP_LOCAL_PORT) != 0)
        return -1;

    /* Build NTP request */
    ntp_packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.li_vn_mode = (0 << 6) | (4 << 3) | 3;  /* LI=0, VN=4 (NTPv4), Mode=3 (client) */

    /* Send request */
    int ret = udp_send(ntp_ip, NTP_PORT, NTP_LOCAL_PORT,
                       (const uint8_t *)&pkt, sizeof(pkt));
    if (ret != 0) {
        udp_unbind(NTP_LOCAL_PORT);
        return -1;
    }

    /* Wait for response (3 second timeout) */
    uint8_t resp[64];
    size_t resp_len = sizeof(resp);
    uint8_t src_ip[4];
    uint16_t src_port;
    ret = udp_recv(NTP_LOCAL_PORT, resp, &resp_len, src_ip, &src_port, 3000);
    udp_unbind(NTP_LOCAL_PORT);

    if (ret != 0 || resp_len < 48)
        return -1;

    /* Parse NTP response */
    ntp_packet_t *reply = (ntp_packet_t *)resp;
    uint32_t ntp_time = ntohl(reply->tx_ts_sec);

    /* Convert NTP timestamp (seconds since 1900) to Unix (seconds since 1970) */
    uint32_t unix_time = ntp_time - NTP_EPOCH_OFFSET;

    /* Apply timezone offset */
    unix_time += tz_offset_seconds();

    datetime_t dt;
    unix_to_datetime(unix_time, &dt);

    /* Update system clock */
    system_config_t *cfg = config_get();
    memcpy(&cfg->datetime, &dt, sizeof(datetime_t));

    return 0;
}

uint32_t rtc_get_epoch(void) {
    datetime_t dt;
    rtc_read(&dt);
    return datetime_to_epoch(&dt);
}
