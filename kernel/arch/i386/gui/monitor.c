#include <kernel/monitor_app.h>
#include <kernel/gfx.h>
#include <kernel/wm.h>
#include <kernel/desktop.h>
#include <kernel/ui_widget.h>
#include <kernel/ui_theme.h>
#include <kernel/ui_event.h>
#include <kernel/mouse.h>
#include <kernel/pci.h>
#include <kernel/net.h>
#include <kernel/idt.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MON_MAX_PCI 32

static int w_tabs, w_list;

/* PCI data */
static pci_device_info_t pci_devs[MON_MAX_PCI];
static int pci_count;

/* List item storage */
#define MAX_LIST_ITEMS 40
static const char *list_items[MAX_LIST_ITEMS];
static char list_bufs[MAX_LIST_ITEMS][128];
static int list_count;

static const char *tab_labels[] = { "1:Hardware", "2:Network", "3:Memory" };

static const char *pci_class_name(uint8_t class_code) {
    switch (class_code) {
    case 0x00: return "Unclassified";
    case 0x01: return "Storage";
    case 0x02: return "Network";
    case 0x03: return "Display";
    case 0x04: return "Multimedia";
    case 0x05: return "Memory";
    case 0x06: return "Bridge";
    case 0x07: return "Communication";
    case 0x08: return "System";
    case 0x09: return "Input";
    case 0x0C: return "Serial Bus";
    default:   return "Other";
    }
}

static void hex4(char *dst, uint16_t val) {
    const char *hex = "0123456789abcdef";
    dst[0] = hex[(val >> 12) & 0xF];
    dst[1] = hex[(val >> 8) & 0xF];
    dst[2] = hex[(val >> 4) & 0xF];
    dst[3] = hex[val & 0xF];
    dst[4] = '\0';
}

static void populate_hardware(void) {
    list_count = 0;
    snprintf(list_bufs[list_count], 128, "PCI Devices:");
    list_items[list_count] = list_bufs[list_count]; list_count++;

    for (int i = 0; i < pci_count && list_count < MAX_LIST_ITEMS; i++) {
        pci_device_info_t *d = &pci_devs[i];
        char vid[5], did[5];
        hex4(vid, d->vendor_id);
        hex4(did, d->device_id);
        snprintf(list_bufs[list_count], 128, " %d:%d  %s:%s  %s",
                 d->bus, d->device, vid, did, pci_class_name(d->class_code));
        list_items[list_count] = list_bufs[list_count]; list_count++;
    }
}

static void populate_network(void) {
    list_count = 0;
    net_config_t *cfg = net_get_config();

    snprintf(list_bufs[list_count], 128, "Network Interface:");
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  Link: %s", cfg->link_up ? "UP" : "DOWN");
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  MAC: %x:%x:%x:%x:%x:%x",
             cfg->mac[0], cfg->mac[1], cfg->mac[2],
             cfg->mac[3], cfg->mac[4], cfg->mac[5]);
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  IP: %d.%d.%d.%d",
             cfg->ip[0], cfg->ip[1], cfg->ip[2], cfg->ip[3]);
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  Netmask: %d.%d.%d.%d",
             cfg->netmask[0], cfg->netmask[1], cfg->netmask[2], cfg->netmask[3]);
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  Gateway: %d.%d.%d.%d",
             cfg->gateway[0], cfg->gateway[1], cfg->gateway[2], cfg->gateway[3]);
    list_items[list_count] = list_bufs[list_count]; list_count++;
}

static void populate_memory(void) {
    list_count = 0;

    snprintf(list_bufs[list_count], 128, "Memory:");
    list_items[list_count] = list_bufs[list_count]; list_count++;

    snprintf(list_bufs[list_count], 128, "  Physical RAM: %dMB",
             (int)gfx_get_system_ram_mb());
    list_items[list_count] = list_bufs[list_count]; list_count++;

    size_t used = heap_used();
    size_t total = heap_total();
    snprintf(list_bufs[list_count], 128, "  Heap Used: %dKB / %dKB",
             (int)(used / 1024), (int)(total / 1024));
    list_items[list_count] = list_bufs[list_count]; list_count++;

    size_t fb_size = gfx_width() * gfx_height() * 4;
    snprintf(list_bufs[list_count], 128, "  Framebuffer: %dKB (%dx%d)",
             (int)(fb_size / 1024), (int)gfx_width(), (int)gfx_height());
    list_items[list_count] = list_bufs[list_count]; list_count++;
}

static void refresh_tab(ui_window_t *win) {
    ui_widget_t *tabs = ui_get_widget(win, w_tabs);
    if (!tabs) return;

    switch (tabs->tabs.active) {
    case 0: populate_hardware(); break;
    case 1: populate_network();  break;
    case 2: populate_memory();   break;
    }

    ui_widget_t *list = ui_get_widget(win, w_list);
    if (list) {
        list->list.items = list_items;
        list->list.count = list_count;
        list->list.scroll = 0;
        list->list.selected = 0;
    }
    win->dirty = 1;
}

static void on_tab_change(ui_window_t *win, int idx) {
    (void)idx;
    refresh_tab(win);
}

void app_monitor_on_event(ui_window_t *win, ui_event_t *ev) {
    if (ev->type == UI_EVENT_KEY_PRESS) {
        ui_widget_t *tabs = ui_get_widget(win, w_tabs);
        if (tabs) {
            char k = ev->key.key;
            if (k == '1') { tabs->tabs.active = 0; refresh_tab(win); }
            if (k == '2') { tabs->tabs.active = 1; refresh_tab(win); }
            if (k == '3') { tabs->tabs.active = 2; refresh_tab(win); }
        }
    }
}

ui_window_t *app_monitor_create(void) {
    int fb_w = (int)gfx_width(), fb_h = (int)gfx_height();
    int win_w = fb_w - 300;
    int win_h = fb_h - TASKBAR_H - 80;

    pci_count = pci_enumerate_devices(pci_devs, MON_MAX_PCI);

    ui_window_t *win = ui_window_create(150, 30, win_w, win_h, "System Monitor");
    if (!win) return 0;

    int cw, ch;
    wm_get_canvas(win->wm_id, &cw, &ch);

    /* Tabs */
    w_tabs = ui_add_tabs(win, 0, 0, cw, 28, tab_labels, 3);
    ui_widget_t *tabs = ui_get_widget(win, w_tabs);
    if (tabs) tabs->tabs.on_change = on_tab_change;

    /* Content list */
    w_list = ui_add_list(win, 0, 28, cw, ch - 28, NULL, 0);

    populate_hardware();
    refresh_tab(win);

    /* Auto-focus first focusable widget */
    if (win->focused_widget < 0)
        ui_focus_next(win);

    return win;
}

void app_monitor(void) {
    ui_window_t *win = app_monitor_create();
    if (!win) return;
    ui_app_run(win, app_monitor_on_event);
    ui_window_destroy(win);
}
