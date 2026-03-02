#include <kernel/clipboard.h>
#include <kernel/msgbus.h>
#include <string.h>

static char clip_buf[CLIPBOARD_MAX];
static size_t clip_len;

void clipboard_copy(const char *text, size_t len) {
    if (!text || len == 0) return;
    if (len >= CLIPBOARD_MAX) len = CLIPBOARD_MAX - 1;
    memcpy(clip_buf, text, len);
    clip_buf[len] = '\0';
    clip_len = len;
    msgbus_publish_str(MSGBUS_TOPIC_CLIPBOARD_CHANGED, clip_buf);
}

const char *clipboard_get(size_t *len) {
    if (len) *len = clip_len;
    return clip_buf;
}

int clipboard_has_content(void) {
    return clip_len > 0;
}

void clipboard_clear(void) {
    clip_len = 0;
    clip_buf[0] = '\0';
}
