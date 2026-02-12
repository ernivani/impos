#ifndef _KERNEL_CONFIG_H
#define _KERNEL_CONFIG_H

#include <stdint.h>

#define CONFIG_FILE "/etc/config"
#define HISTORY_FILE "/etc/history"

#ifndef KB_LAYOUT_FR
#define KB_LAYOUT_FR  0
#endif
#ifndef KB_LAYOUT_US
#define KB_LAYOUT_US  1
#endif

/* System time and date structure */
typedef struct {
    uint16_t year;
    uint8_t month;    /* 1-12 */
    uint8_t day;      /* 1-31 */
    uint8_t hour;     /* 0-23 */
    uint8_t minute;   /* 0-59 */
    uint8_t second;   /* 0-59 */
} datetime_t;

/* System configuration structure */
typedef struct {
    uint8_t keyboard_layout;  /* KB_LAYOUT_US or KB_LAYOUT_FR */
    datetime_t datetime;
    uint32_t uptime_seconds;  /* Time since boot */
    char timezone[32];        /* e.g., "UTC", "Europe/Paris" */
    uint8_t use_24h_format;   /* 1 for 24h, 0 for 12h AM/PM */
    uint8_t auto_dst;         /* 1 = auto summer/winter time, 0 = manual */
} system_config_t;

/* Initialize configuration system */
void config_initialize(void);

/* Load/save configuration */
int config_load(void);
int config_save(void);

/* Get/set configuration */
system_config_t* config_get(void);
void config_set_keyboard_layout(uint8_t layout);
uint8_t config_get_keyboard_layout(void);

/* Time/date functions */
void config_get_datetime(datetime_t* dt);
void config_set_datetime(const datetime_t* dt);
void config_tick_second(void);  /* Called every second to update time */
const char* config_get_timezone(void);
void config_set_timezone(const char* tz);

/* History functions */
int config_save_history(void);
int config_load_history(void);

#endif
