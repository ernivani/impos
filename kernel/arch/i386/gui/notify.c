/* notify.c — Desktop toast notification system (Phase 8).
 *
 * Toasts appear in the top-right corner below the menubar.
 * Max 3 visible simultaneously, 16 queued.
 * Each toast has its own compositor overlay surface.
 * Slide-in from right, auto-dismiss after timeout, click to dismiss.
 */
#include <kernel/notify.h>
#include <kernel/compositor.h>
#include <kernel/gfx.h>
#include <kernel/ui_theme.h>
#include <kernel/anim.h>
#include <kernel/menubar.h>
#include <kernel/msgbus.h>
#include <string.h>

/* Urgency accent colors: info=blue, success=green, warning=orange, error=red */
static const uint32_t urgency_colors[4] = {
    0xFF3478F6,  /* info    */
    0xFFA6E3A1,  /* success */
    0xFFFF9500,  /* warning */
    0xFFF38BA8,  /* error   */
};

typedef struct {
    char title[48];
    char body[128];
    int urgency;
    int active;       /* slot in use */
    int visible;      /* promoted to screen */
    int dismissing;   /* fade-out in progress */
    uint32_t expire_tick;
    int slide_x;      /* animated: current x offset (starts off-screen) */
    int alpha_val;    /* animated: 0-255 */
    int anim_slide;   /* tween id or -1 */
    int anim_alpha;   /* tween id or -1 */
    comp_surface_t *surf;
} notification_t;

static notification_t slots[NOTIFY_MAX_QUEUED];
static int screen_w;

/* ── Internal helpers ─────────────────────────────────────────── */

static int count_visible(void) {
    int c = 0;
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++)
        if (slots[i].active && slots[i].visible && !slots[i].dismissing) c++;
    return c;
}

static int count_visible_and_dismissing(void) {
    int c = 0;
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++)
        if (slots[i].active && slots[i].visible) c++;
    return c;
}

static void paint_toast(notification_t *n) {
    if (!n->surf) return;
    gfx_surface_t gs = comp_surface_lock(n->surf);

    /* Background: dark rounded rect */
    uint32_t bg = 0xE6181825;  /* ~90% opacity dark */
    for (int y = 0; y < NOTIFY_H; y++)
        for (int x = 0; x < NOTIFY_W; x++)
            gs.buf[y * gs.pitch + x] = bg;

    /* Urgency accent bar on the left (4px wide) */
    uint32_t accent = urgency_colors[n->urgency & 3];
    for (int y = 4; y < NOTIFY_H - 4; y++)
        for (int x = 0; x < 4; x++)
            gs.buf[y * gs.pitch + x] = accent;

    /* Title text */
    gfx_surf_draw_string_smooth(&gs, 12, 10, n->title,
                                ui_theme.text_primary, 1);

    /* Body text */
    gfx_surf_draw_string_smooth(&gs, 12, 32, n->body,
                                ui_theme.text_secondary, 1);

    /* Bottom border: subtle line */
    for (int x = 4; x < NOTIFY_W - 4; x++)
        gs.buf[(NOTIFY_H - 1) * gs.pitch + x] = 0x20FFFFFF;

    comp_surface_damage_all(n->surf);
}

static int slot_y(int visible_idx) {
    return MENUBAR_HEIGHT + NOTIFY_MARGIN + visible_idx * (NOTIFY_H + NOTIFY_MARGIN);
}

static void promote_one(uint32_t now) {
    if (count_visible() >= NOTIFY_MAX_VISIBLE) return;

    /* Find first queued (active but not visible) */
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++) {
        if (!slots[i].active || slots[i].visible) continue;

        notification_t *n = &slots[i];
        int vis_idx = count_visible_and_dismissing();

        /* Create compositor surface */
        n->surf = comp_surface_create(NOTIFY_W, NOTIFY_H, COMP_LAYER_OVERLAY);
        if (!n->surf) return;

        /* Position: right edge, below menubar */
        int target_x = screen_w - NOTIFY_W - NOTIFY_MARGIN;
        int y = slot_y(vis_idx);
        comp_surface_move(n->surf, screen_w, y); /* start off-screen */

        n->visible = 1;
        n->slide_x = screen_w;
        n->alpha_val = 255;
        n->anim_slide = anim_start(&n->slide_x, screen_w, target_x, 300, ANIM_EASE_OUT);
        n->anim_alpha = -1;

        /* Set expiry */
        if (n->expire_tick == 0)
            n->expire_tick = now + 600; /* default: 5 seconds at 120Hz */

        paint_toast(n);
        return;
    }
}

static void dismiss_slot(int i) {
    notification_t *n = &slots[i];
    if (!n->active) return;

    if (n->visible && !n->dismissing) {
        /* Start fade-out */
        n->dismissing = 1;
        n->anim_alpha = anim_start(&n->alpha_val, 255, 0, 200, ANIM_EASE_IN);
        if (n->anim_slide >= 0) {
            anim_cancel(n->anim_slide);
            n->anim_slide = -1;
        }
    } else if (!n->visible) {
        /* Queued but not visible: just free */
        n->active = 0;
    }
}

static void finalize_dismiss(int i) {
    notification_t *n = &slots[i];
    if (n->surf) {
        comp_surface_destroy(n->surf);
        n->surf = NULL;
    }
    n->active = 0;
    n->visible = 0;
    n->dismissing = 0;
}

/* Reposition visible toasts to fill gaps */
static void reflow(void) {
    int idx = 0;
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++) {
        if (!slots[i].active || !slots[i].visible) continue;
        int y = slot_y(idx);
        if (slots[i].surf) {
            int cur_x = slots[i].surf->screen_x;
            comp_surface_move(slots[i].surf, cur_x, y);
        }
        idx++;
    }
}

/* ── Message bus handler ──────────────────────────────────────── */

static void on_notify_msg(const msgbus_msg_t *msg, void *ctx) {
    (void)ctx;
    if (msg->type == MSGBUS_TYPE_STR && msg->sval)
        notify_post("Notification", msg->sval, NOTIFY_INFO, 0);
}

/* ── Public API ───────────────────────────────────────────────── */

void notify_init(void) {
    memset(slots, 0, sizeof(slots));
    screen_w = (int)gfx_width();
    msgbus_subscribe(MSGBUS_TOPIC_NOTIFY, on_notify_msg, NULL);
}

notify_id_t notify_post(const char *title, const char *body,
                        int urgency, uint32_t timeout_ticks) {
    /* Find free slot */
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++) {
        if (slots[i].active) continue;

        notification_t *n = &slots[i];
        memset(n, 0, sizeof(*n));
        n->active = 1;
        n->urgency = urgency;
        n->anim_slide = -1;
        n->anim_alpha = -1;

        if (title) {
            int j = 0;
            while (j < 47 && title[j]) { n->title[j] = title[j]; j++; }
            n->title[j] = '\0';
        }
        if (body) {
            int j = 0;
            while (j < 127 && body[j]) { n->body[j] = body[j]; j++; }
            n->body[j] = '\0';
        }

        /* Store raw timeout; promote_one() will compute absolute expiry */
        n->expire_tick = timeout_ticks; /* 0 = use default in promote_one */

        return i;
    }
    return -1;
}

void notify_dismiss(notify_id_t id) {
    if (id < 0 || id >= NOTIFY_MAX_QUEUED) return;
    dismiss_slot(id);
}

void notify_dismiss_all(void) {
    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++)
        if (slots[i].active)
            dismiss_slot(i);
}

int notify_visible_count(void) {
    return count_visible();
}

void notify_tick(uint32_t now) {
    int any_removed = 0;

    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++) {
        if (!slots[i].active || !slots[i].visible) continue;
        notification_t *n = &slots[i];

        /* Update slide position */
        if (n->surf && n->anim_slide >= 0) {
            comp_surface_move(n->surf, n->slide_x, n->surf->screen_y);
            if (!anim_active(n->anim_slide))
                n->anim_slide = -1;
        }

        /* Update alpha */
        if (n->surf && n->anim_alpha >= 0) {
            uint8_t a = (n->alpha_val < 0) ? 0 :
                        (n->alpha_val > 255) ? 255 : (uint8_t)n->alpha_val;
            comp_surface_set_alpha(n->surf, a);
            if (!anim_active(n->anim_alpha)) {
                n->anim_alpha = -1;
                if (n->dismissing) {
                    finalize_dismiss(i);
                    any_removed = 1;
                    continue;
                }
            }
        }

        /* Check expiry */
        if (!n->dismissing && n->expire_tick > 0 && now >= n->expire_tick) {
            dismiss_slot(i);
        }
    }

    if (any_removed)
        reflow();

    /* Promote queued notifications */
    promote_one(now);
}

int notify_mouse(int mx, int my, int btn_down, int btn_up) {
    (void)btn_down;
    if (!btn_up) return 0;

    for (int i = 0; i < NOTIFY_MAX_QUEUED; i++) {
        if (!slots[i].active || !slots[i].visible || !slots[i].surf) continue;
        notification_t *n = &slots[i];
        int sx = n->surf->screen_x;
        int sy = n->surf->screen_y;
        if (mx >= sx && mx < sx + NOTIFY_W &&
            my >= sy && my < sy + NOTIFY_H) {
            dismiss_slot(i);
            return 1;
        }
    }
    return 0;
}
