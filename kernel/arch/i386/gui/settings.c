#include <kernel/settings_app.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/hostname.h>
#include <kernel/config.h>
#include <kernel/user.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* ═══ Tab definitions ══════════════════════════════════════════ */

#define TAB_GENERAL  0
#define TAB_DISPLAY  1
#define TAB_NETWORK  2
#define TAB_USERS    3
#define TAB_ABOUT    4
#define NUM_TABS     5

static const char *tab_labels[NUM_TABS] = {
    "General", "Display", "Network", "Users", "About"
};

#define SIDEBAR_W    140

/* Widget indices */
static int w_sidebar;
static int active_tab;

/* Per-tab widget range: [tab_start[t], tab_end[t]) */
static int tab_start[NUM_TABS];
static int tab_end[NUM_TABS];

/* General tab */
static int w_kbd_toggle;
static int w_hostname_input;
static int w_24h_toggle;

/* Display tab */
static int w_res_label;
static int w_fb_label;

/* Network tab */
static int w_link_label;
static int w_mac_label;
static int w_ip_label;
static int w_mask_label;
static int w_gw_label;

/* Users tab */
static int w_cur_user_label;
static int w_user_list;
static char user_list_bufs[MAX_USERS][48];
static const char *user_list_items[MAX_USERS];
static int user_list_count;

/* About tab */
static int w_os_label;
static int w_uptime_label;
static int w_mem_bar;
static int w_mem_label;
static int w_build_label;

/* Dynamic strings */
static char link_str[64];
static char mac_str[48];
static char ip_str[48];
static char mask_str[48];
static char gw_str[48];
static char res_str[64];
static char fb_str[64];
static char uptime_str[64];
static char mem_str[64];
static char cur_user_str[64];

/* ═══ Helpers ══════════════════════════════════════════════════ */

static void fmt_ip(char *buf, const uint8_t ip[4]) {
    snprintf(buf, 48, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static void fmt_mac(char *buf, const uint8_t mac[6]) {
    snprintf(buf, 48, "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void show_tab(ui_window_t *win, int tab) {
    /* Hide all tab content */
    for (int t = 0; t < NUM_TABS; t++)
        ui_widget_set_visible_range(win, tab_start[t], tab_end[t], 0);
    /* Show selected tab */
    ui_widget_set_visible_range(win, tab_start[tab], tab_end[tab], 1);
    active_tab = tab;
}

/* ═══ Sidebar custom draw ═════════════════════════════════════ */

static void settings_draw_sidebar(ui_window_t *win, int widget_idx,
                                   uint32_t *canvas, int cw, int ch) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return;
    int x0 = wg->x, y0 = wg->y;
    int w = wg->w, h = wg->h;

    /* Background */
    gfx_buf_fill_rect(canvas, cw, ch, x0, y0, w, h, ui_theme.surface);
    /* Right border */
    gfx_buf_fill_rect(canvas, cw, ch, x0 + w - 1, y0, 1, h, ui_theme.border);

    /* Title */
    gfx_buf_draw_string(canvas, cw, ch, x0 + 12, y0 + 10,
                         "Settings", ui_theme.text_secondary, ui_theme.surface);

    /* Tab items */
    int row_h = 28;
    int ry = y0 + 34;
    for (int i = 0; i < NUM_TABS; i++) {
        int selected = (i == active_tab);

        if (selected) {
            /* Accent bar on left + highlight background */
            gfx_buf_fill_rect(canvas, cw, ch, x0, ry, 3, row_h, ui_theme.accent);
            gfx_buf_fill_rect(canvas, cw, ch, x0 + 3, ry, w - 4, row_h,
                               GFX_RGB(38, 38, 52));
        }

        uint32_t tc = selected ? GFX_RGB(255, 255, 255) : ui_theme.text_secondary;
        gfx_buf_draw_string(canvas, cw, ch, x0 + 16, ry + (row_h - FONT_H) / 2,
                             tab_labels[i], tc,
                             selected ? GFX_RGB(38, 38, 52) : ui_theme.surface);

        ry += row_h;
    }
}

static int settings_sidebar_event(ui_window_t *win, int widget_idx, ui_event_t *ev) {
    ui_widget_t *wg = ui_get_widget(win, widget_idx);
    if (!wg) return 0;

    if (ev->type == UI_EVENT_MOUSE_DOWN) {
        int wy = ev->mouse.wy - wg->y;
        int row_h = 28;
        int ry = 34;
        for (int i = 0; i < NUM_TABS; i++) {
            if (wy >= ry && wy < ry + row_h) {
                show_tab(win, i);
                /* Refresh data for the selected tab */
                extern void refresh_network(ui_window_t *win);
                extern void refresh_users(ui_window_t *win);
                extern void refresh_about(ui_window_t *win);
                if (i == TAB_NETWORK) refresh_network(win);
                else if (i == TAB_USERS) refresh_users(win);
                else if (i == TAB_ABOUT) refresh_about(win);
                win->dirty = 1;
                return 1;
            }
            ry += row_h;
        }
    }

    if (ev->type == UI_EVENT_KEY_PRESS) {
        if (ev->key.key == KEY_UP && active_tab > 0) {
            show_tab(win, active_tab - 1);
            win->dirty = 1;
            return 1;
        }
        if (ev->key.key == KEY_DOWN && active_tab < NUM_TABS - 1) {
            show_tab(win, active_tab + 1);
            win->dirty = 1;
            return 1;
        }
    }

    return 0;
}

/* ═══ Data refresh ════════════════════════════════════════════ */

void refresh_network(ui_window_t *win) {
    net_config_t *cfg = net_get_config();
    ui_widget_t *wg;

    snprintf(link_str, sizeof(link_str), "Link: %s", cfg->link_up ? "UP" : "DOWN");
    wg = ui_get_widget(win, w_link_label);
    if (wg) strncpy(wg->label.text, link_str, UI_TEXT_MAX - 1);

    char tmp[20];
    fmt_mac(mac_str, cfg->mac);
    char mac_full[64];
    snprintf(mac_full, sizeof(mac_full), "MAC:  %s", mac_str);
    wg = ui_get_widget(win, w_mac_label);
    if (wg) strncpy(wg->label.text, mac_full, UI_TEXT_MAX - 1);

    fmt_ip(tmp, cfg->ip);
    snprintf(ip_str, sizeof(ip_str), "IP:   %s", tmp);
    wg = ui_get_widget(win, w_ip_label);
    if (wg) strncpy(wg->label.text, ip_str, UI_TEXT_MAX - 1);

    fmt_ip(tmp, cfg->netmask);
    snprintf(mask_str, sizeof(mask_str), "Mask: %s", tmp);
    wg = ui_get_widget(win, w_mask_label);
    if (wg) strncpy(wg->label.text, mask_str, UI_TEXT_MAX - 1);

    fmt_ip(tmp, cfg->gateway);
    snprintf(gw_str, sizeof(gw_str), "GW:   %s", tmp);
    wg = ui_get_widget(win, w_gw_label);
    if (wg) strncpy(wg->label.text, gw_str, UI_TEXT_MAX - 1);
}

void refresh_users(ui_window_t *win) {
    const char *cur = user_get_current();
    snprintf(cur_user_str, sizeof(cur_user_str), "Current: %s", cur ? cur : "none");
    ui_widget_t *wg = ui_get_widget(win, w_cur_user_label);
    if (wg) strncpy(wg->label.text, cur_user_str, UI_TEXT_MAX - 1);

    user_list_count = 0;
    int uc = user_count();
    for (int i = 0; i < uc && user_list_count < MAX_USERS; i++) {
        user_t *u = user_get_by_index(i);
        if (!u || !u->active) continue;
        snprintf(user_list_bufs[user_list_count], 48, "  %s (uid:%d)",
                 u->username, u->uid);
        user_list_items[user_list_count] = user_list_bufs[user_list_count];
        user_list_count++;
    }
    wg = ui_get_widget(win, w_user_list);
    if (wg) {
        wg->list.items = user_list_items;
        wg->list.count = user_list_count;
    }
}

void refresh_about(ui_window_t *win) {
    uint32_t ticks = pit_get_ticks();
    uint32_t secs = ticks / 100;
    snprintf(uptime_str, sizeof(uptime_str), "Uptime: %dh %dm %ds",
             (int)(secs / 3600), (int)((secs % 3600) / 60), (int)(secs % 60));
    ui_widget_t *wg = ui_get_widget(win, w_uptime_label);
    if (wg) strncpy(wg->label.text, uptime_str, UI_TEXT_MAX - 1);

    size_t used = heap_used();
    size_t total = heap_total();
    int pct = total > 0 ? (int)((uint64_t)used * 100 / total) : 0;
    snprintf(mem_str, sizeof(mem_str), "Heap: %dKB / %dKB (%d%%)",
             (int)(used / 1024), (int)(total / 1024), pct);
    wg = ui_get_widget(win, w_mem_bar);
    if (wg) wg->progress.value = pct;
    wg = ui_get_widget(win, w_mem_label);
    if (wg) strncpy(wg->label.text, mem_str, UI_TEXT_MAX - 1);
}

/* ═══ Callbacks ════════════════════════════════════════════════ */

static void on_kbd_toggle(ui_window_t *win, int idx) {
    ui_widget_t *wg = ui_get_widget(win, idx);
    if (!wg) return;
    keyboard_set_layout(wg->toggle.on ? KB_LAYOUT_FR : KB_LAYOUT_US);
    config_set_keyboard_layout(wg->toggle.on ? KB_LAYOUT_FR : KB_LAYOUT_US);
    config_save();
}

static void on_24h_toggle(ui_window_t *win, int idx) {
    ui_widget_t *wg = ui_get_widget(win, idx);
    if (!wg) return;
    system_config_t *cfg = config_get();
    cfg->use_24h_format = wg->toggle.on ? 1 : 0;
    config_save();
}

static void on_hostname_submit(ui_window_t *win, int idx) {
    ui_widget_t *wg = ui_get_widget(win, idx);
    if (!wg) return;
    hostname_set(wg->textinput.text);
    hostname_save();
}

/* ═══ Create ══════════════════════════════════════════════════ */

ui_window_t *app_settings_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = 650, win_h = 500;

    active_tab = TAB_GENERAL;

    ui_window_t *win = ui_window_create(fb_w / 2 - win_w / 2,
                                         fb_h / 2 - win_h / 2 - 20,
                                         win_w, win_h, "Settings");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    int pad = ui_theme.padding;
    int cx = SIDEBAR_W + pad;       /* content x start */
    int content_w = cw - SIDEBAR_W - 2 * pad;
    int y_content = 8;

    /* Sidebar (custom widget) */
    w_sidebar = ui_add_custom(win, 0, 0, SIDEBAR_W, ch,
                               settings_draw_sidebar, settings_sidebar_event, NULL);

    /* ─── General tab ─────────────────────────────────── */
    tab_start[TAB_GENERAL] = win->widget_count;

    ui_add_card(win, cx, y_content, content_w, 44, "Keyboard", 0, 0);
    w_kbd_toggle = ui_add_toggle(win, cx + 12, y_content + 26, content_w - 24, 14,
                                  "FR (AZERTY)", keyboard_get_layout() == KB_LAYOUT_FR);
    ui_widget_t *kt = ui_get_widget(win, w_kbd_toggle);
    if (kt) kt->toggle.on_change = on_kbd_toggle;

    int y2 = y_content + 54;
    ui_add_card(win, cx, y2, content_w, 68, "Hostname", 0, 0);
    w_hostname_input = ui_add_textinput(win, cx + 12, y2 + 28, content_w - 24, 28,
                                         "hostname", MAX_HOSTNAME, 0);
    ui_widget_t *hi = ui_get_widget(win, w_hostname_input);
    if (hi) {
        hi->textinput.on_submit = on_hostname_submit;
        const char *h = hostname_get();
        if (h) strncpy(hi->textinput.text, h, UI_TEXT_MAX - 1);
        hi->textinput.cursor = (int)strlen(hi->textinput.text);
    }

    int y3 = y2 + 78;
    ui_add_card(win, cx, y3, content_w, 44, "Time Format", 0, 0);
    system_config_t *scfg = config_get();
    w_24h_toggle = ui_add_toggle(win, cx + 12, y3 + 26, content_w - 24, 14,
                                  "24-hour clock", scfg->use_24h_format);
    ui_widget_t *tt = ui_get_widget(win, w_24h_toggle);
    if (tt) tt->toggle.on_change = on_24h_toggle;

    tab_end[TAB_GENERAL] = win->widget_count;

    /* ─── Display tab ─────────────────────────────────── */
    tab_start[TAB_DISPLAY] = win->widget_count;

    ui_add_card(win, cx, y_content, content_w, 80, "Display Info", 0, 0);
    snprintf(res_str, sizeof(res_str), "Resolution: %dx%d @ %dbpp",
             (int)gfx_width(), (int)gfx_height(), (int)gfx_bpp());
    w_res_label = ui_add_label(win, cx + 12, y_content + 30, content_w - 24, 20,
                                res_str, 0);
    snprintf(fb_str, sizeof(fb_str), "Pitch: %d bytes, RAM: %dMB",
             (int)gfx_pitch(), (int)gfx_get_system_ram_mb());
    w_fb_label = ui_add_label(win, cx + 12, y_content + 52, content_w - 24, 20,
                               fb_str, ui_theme.text_sub);

    tab_end[TAB_DISPLAY] = win->widget_count;

    /* ─── Network tab ─────────────────────────────────── */
    tab_start[TAB_NETWORK] = win->widget_count;

    ui_add_card(win, cx, y_content, content_w, 150, "Network Status", 0, 0);
    int ny = y_content + 30;
    w_link_label = ui_add_label(win, cx + 12, ny, content_w - 24, 20, "Link: ...", 0);
    ny += 22;
    w_mac_label = ui_add_label(win, cx + 12, ny, content_w - 24, 20, "MAC:  ...", ui_theme.text_sub);
    ny += 22;
    w_ip_label = ui_add_label(win, cx + 12, ny, content_w - 24, 20, "IP:   ...", ui_theme.text_sub);
    ny += 22;
    w_mask_label = ui_add_label(win, cx + 12, ny, content_w - 24, 20, "Mask: ...", ui_theme.text_sub);
    ny += 22;
    w_gw_label = ui_add_label(win, cx + 12, ny, content_w - 24, 20, "GW:   ...", ui_theme.text_sub);
    refresh_network(win);

    tab_end[TAB_NETWORK] = win->widget_count;

    /* ─── Users tab ───────────────────────────────────── */
    tab_start[TAB_USERS] = win->widget_count;

    ui_add_card(win, cx, y_content, content_w, 36, "Current User", 0, 0);
    w_cur_user_label = ui_add_label(win, cx + 12, y_content + 26, content_w - 24, 16,
                                     "", 0);

    ui_add_card(win, cx, y_content + 46, content_w, ch - y_content - 56, "All Users", 0, 0);
    w_user_list = ui_add_list(win, cx + 4, y_content + 72,
                               content_w - 8, ch - y_content - 82, NULL, 0);
    refresh_users(win);

    tab_end[TAB_USERS] = win->widget_count;

    /* ─── About tab ───────────────────────────────────── */
    tab_start[TAB_ABOUT] = win->widget_count;

    ui_add_card(win, cx, y_content, content_w, 160, "System Information", 0, 0);
    int ay = y_content + 30;
    w_os_label = ui_add_label(win, cx + 12, ay, content_w - 24, 20,
                               "ImposOS v1.0 (i386)", ui_theme.accent);
    ay += 24;
    w_uptime_label = ui_add_label(win, cx + 12, ay, content_w - 24, 20, "Uptime: ...", 0);
    ay += 28;
    w_mem_bar = ui_add_progress(win, cx + 12, ay, content_w - 24, 14, 0, NULL);
    ay += 22;
    w_mem_label = ui_add_label(win, cx + 12, ay, content_w - 24, 20, "", ui_theme.text_sub);
    ay += 24;
    w_build_label = ui_add_label(win, cx + 12, ay, content_w - 24, 20,
                                  "Built with i686-elf-gcc, GRUB multiboot", ui_theme.text_dim);
    refresh_about(win);

    tab_end[TAB_ABOUT] = win->widget_count;

    /* Show only General tab initially */
    show_tab(win, TAB_GENERAL);

    /* Auto-focus sidebar */
    win->focused_widget = w_sidebar;

    return win;
}

/* ═══ Event handler ═══════════════════════════════════════════ */

void app_settings_on_event(ui_window_t *win, ui_event_t *ev) {
    (void)ev;
    /* Refresh About data on every event so uptime updates */
    if (active_tab == TAB_ABOUT) {
        refresh_about(win);
        win->dirty = 1;
    }
}

/* ═══ Standalone entry point ══════════════════════════════════ */

void app_settings(void) {
    ui_window_t *win = app_settings_create();
    if (!win) return;
    ui_app_run(win, app_settings_on_event);
    ui_window_destroy(win);
}
