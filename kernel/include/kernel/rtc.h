#ifndef _KERNEL_RTC_H
#define _KERNEL_RTC_H

#include <stdint.h>
#include <kernel/config.h>

/* Timezone entry */
typedef struct {
    const char *name;       /* e.g. "Europe/Paris" */
    const char *city;       /* e.g. "Paris" */
    int std_offset;         /* standard offset from UTC in seconds */
    int has_dst;            /* 1 if this zone observes DST */
} tz_entry_t;

/* Read current date/time from CMOS RTC hardware */
void rtc_read(datetime_t *dt);

/* Initialize RTC — seeds system clock from hardware */
void rtc_init(void);

/* Sync time via NTP (pool.ntp.org). Returns 0 on success. */
int rtc_ntp_sync(void);

/* Get a monotonic timestamp (seconds since 2000-01-01) */
uint32_t rtc_get_epoch(void);

/* Convert epoch back to datetime */
void epoch_to_datetime(uint32_t epoch, datetime_t *dt);

/* Format epoch as "Mon DD HH:MM" into buf (at least 13 bytes) */
void rtc_format_epoch(uint32_t epoch, char *buf, int bufsize);

/* Get timezone database (returns pointer, sets *count) */
const tz_entry_t *rtc_get_tz_db(int *count);

#endif
