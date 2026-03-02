#ifndef _KERNEL_MSGBUS_H
#define _KERNEL_MSGBUS_H

#include <stdint.h>

#define MSGBUS_MAX_SUBS 32

/* Well-known topic names */
#define MSGBUS_TOPIC_THEME_CHANGED     "theme-changed"
#define MSGBUS_TOPIC_APP_LAUNCHED      "app-launched"
#define MSGBUS_TOPIC_APP_CLOSED        "app-closed"
#define MSGBUS_TOPIC_CLIPBOARD_CHANGED "clipboard-changed"
#define MSGBUS_TOPIC_NOTIFY            "notification"
#define MSGBUS_TOPIC_WINDOW_OPENED     "window-opened"
#define MSGBUS_TOPIC_WINDOW_CLOSED     "window-closed"

/* Payload types */
#define MSGBUS_TYPE_NONE 0
#define MSGBUS_TYPE_INT  1
#define MSGBUS_TYPE_STR  2
#define MSGBUS_TYPE_PTR  3

typedef struct {
    const char *topic;
    int type;
    union {
        int ival;
        const char *sval;
        void *pval;
    };
} msgbus_msg_t;

typedef void (*msgbus_handler_t)(const msgbus_msg_t *msg, void *ctx);

void msgbus_init(void);
int  msgbus_subscribe(const char *topic, msgbus_handler_t handler, void *ctx);
void msgbus_unsubscribe(int sub_id);
int  msgbus_publish(const msgbus_msg_t *msg);
int  msgbus_publish_str(const char *topic, const char *value);
int  msgbus_publish_int(const char *topic, int value);

#endif
