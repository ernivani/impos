/* about.c — About / System Info app using widget toolkit (Phase 7.5)
 *
 * Displays system information with progress bars for CPU/memory/disk.
 * Auto-refreshes stats every ~1 second.
 */
#include <kernel/ui_widget.h>
#include <kernel/ui_window.h>
#include <kernel/ui_theme.h>
#include <kernel/gfx.h>
#include <kernel/pmm.h>
#include <kernel/fs.h>
#include <kernel/idt.h>
#include <kernel/task.h>
#include <string.h>
#include <stdio.h>

/* ── State ─────────────────────────────────────────────────────── */

static ui_window_t *about_win = NULL;
static int cpu_prog_idx  = -1;
static int mem_prog_idx  = -1;
static int disk_prog_idx = -1;
static uint32_t about_last_refresh = 0;

/* ── Stats update ──────────────────────────────────────────────── */

static void about_refresh_stats(void)
{
    if (!about_win) return;
    ui_widget_t *w;

    /* CPU: heuristic based on active task count */
    int cpu_pct = 15;
    int ntasks = task_count();
    if (ntasks > 3) cpu_pct = 10 + ntasks * 5;
    if (cpu_pct > 99) cpu_pct = 99;

    w = ui_get_widget(about_win, cpu_prog_idx);
    if (w) {
        w->progress.value = cpu_pct;
        snprintf(w->progress.label, sizeof(w->progress.label),
                 "CPU (%d%%)", cpu_pct);
    }

    /* Memory: PMM frames (4KB each, 65536 total = 256MB) */
    uint32_t free_frames = pmm_free_frame_count();
    uint32_t total_frames = 65536; /* NUM_BLOCKS = 65536 frames */
    uint32_t used_frames = (free_frames < total_frames)
                           ? total_frames - free_frames : 0;
    int mem_pct = (int)(used_frames * 100 / total_frames);

    w = ui_get_widget(about_win, mem_prog_idx);
    if (w) {
        w->progress.value = mem_pct;
        snprintf(w->progress.label, sizeof(w->progress.label),
                 "Memory (%d%%)", mem_pct);
    }

    /* Disk: FS block usage */
    uint32_t free_blk = fs_count_free_blocks();
    uint32_t total_blk = NUM_BLOCKS;
    uint32_t used_blk = (free_blk < total_blk) ? total_blk - free_blk : 0;
    int disk_pct = (int)(used_blk * 100 / total_blk);

    w = ui_get_widget(about_win, disk_prog_idx);
    if (w) {
        w->progress.value = disk_pct;
        snprintf(w->progress.label, sizeof(w->progress.label),
                 "Disk (%d%%)", disk_pct);
    }

    about_win->dirty = 1;
}

/* ── Public API ────────────────────────────────────────────────── */

void app_about_open(void)
{
    if (about_win) {
        ui_window_focus(about_win->wm_id);
        ui_window_raise(about_win->wm_id);
        return;
    }

    int w = 360, h = 380;
    int sw = (int)gfx_width(), sh = (int)gfx_height();
    about_win = uw_create(sw / 2 - w / 2 - 60, sh / 2 - h / 2,
                          w, h, "About ImposOS");
    if (!about_win) return;

    int y = 12;
    int cw = w - 24;

    /* Title */
    ui_add_label(about_win, 12, y, cw, 20, "ImposOS v1.0",
                 ui_theme.text_primary);
    y += 22;

    /* Subtitle */
    ui_add_label(about_win, 12, y, cw, 16, "i386 | 256MB | 120Hz",
                 ui_theme.text_dim);
    y += 24;

    /* Separator */
    ui_add_separator(about_win, 12, y, cw);
    y += 10;

    /* System section */
    ui_add_label(about_win, 12, y, cw, 16, "System", ui_theme.text_dim);
    y += 22;

    cpu_prog_idx = ui_add_progress(about_win, 12, y, cw, 34, 0, "CPU");
    y += 44;

    mem_prog_idx = ui_add_progress(about_win, 12, y, cw, 34, 0, "Memory");
    y += 44;

    disk_prog_idx = ui_add_progress(about_win, 12, y, cw, 34, 0, "Disk");
    y += 48;

    /* Separator */
    ui_add_separator(about_win, 12, y, cw);
    y += 10;

    /* Build info */
    ui_add_label(about_win, 12, y, cw, 16, "Build", ui_theme.text_dim);
    y += 22;

    ui_add_label(about_win, 12, y, cw, 16, "Kernel: ImposOS (i386)",
                 ui_theme.text_secondary);
    y += 20;
    ui_add_label(about_win, 12, y, cw, 16, "Compiler: i686-elf-gcc",
                 ui_theme.text_secondary);
    y += 20;
    ui_add_label(about_win, 12, y, cw, 16, "Shell: /bin/sh",
                 ui_theme.text_secondary);
    y += 20;
    ui_add_label(about_win, 12, y, cw, 16, "Display: 1920x1080x32",
                 ui_theme.text_secondary);

    about_refresh_stats();
    uw_redraw(about_win);
}

int about_tick(int mx, int my, int btn_down, int btn_up)
{
    if (!about_win) return 0;

    /* Auto-refresh stats every ~1 second (120 ticks) */
    uint32_t now = pit_get_ticks();
    if (now - about_last_refresh >= 120) {
        about_last_refresh = now;
        about_refresh_stats();
    }

    int r = uw_tick(about_win, mx, my, btn_down, btn_up, 0);
    if (about_win && about_win->wm_id < 0) {
        about_win = NULL;
        cpu_prog_idx = -1;
        mem_prog_idx = -1;
        disk_prog_idx = -1;
    }
    return r;
}

int about_win_open(void) { return about_win != NULL; }
