/* msgbus.c — Kernel-internal publish/subscribe message bus.
 *
 * Synchronous delivery: handlers run inline during publish().
 * 32 subscription slots, string-based topic matching.
 */
#include <kernel/msgbus.h>
#include <string.h>

typedef struct {
    const char     *topic;
    msgbus_handler_t handler;
    void           *ctx;
    int             active;
} msgbus_sub_t;

static msgbus_sub_t subs[MSGBUS_MAX_SUBS];

void msgbus_init(void) {
    memset(subs, 0, sizeof(subs));
}

int msgbus_subscribe(const char *topic, msgbus_handler_t handler, void *ctx) {
    if (!topic || !handler) return -1;
    for (int i = 0; i < MSGBUS_MAX_SUBS; i++) {
        if (!subs[i].active) {
            subs[i].topic   = topic;
            subs[i].handler = handler;
            subs[i].ctx     = ctx;
            subs[i].active  = 1;
            return i;
        }
    }
    return -1;
}

void msgbus_unsubscribe(int sub_id) {
    if (sub_id < 0 || sub_id >= MSGBUS_MAX_SUBS) return;
    subs[sub_id].active = 0;
}

int msgbus_publish(const msgbus_msg_t *msg) {
    if (!msg || !msg->topic) return 0;
    int delivered = 0;
    for (int i = 0; i < MSGBUS_MAX_SUBS; i++) {
        if (subs[i].active && strcmp(subs[i].topic, msg->topic) == 0) {
            subs[i].handler(msg, subs[i].ctx);
            delivered++;
        }
    }
    return delivered;
}

int msgbus_publish_str(const char *topic, const char *value) {
    msgbus_msg_t msg;
    msg.topic = topic;
    msg.type  = MSGBUS_TYPE_STR;
    msg.sval  = value;
    return msgbus_publish(&msg);
}

int msgbus_publish_int(const char *topic, int value) {
    msgbus_msg_t msg;
    msg.topic = topic;
    msg.type  = MSGBUS_TYPE_INT;
    msg.ival  = value;
    return msgbus_publish(&msg);
}
