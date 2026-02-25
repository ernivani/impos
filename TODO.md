# ImposOS Desktop Rewrite -- Modern GUI Architecture

## Vision

A clean, layered GUI for bare-metal i386 that feels modern: composited
windows, macOS-style chrome, event-driven apps, smooth animations.
Radically simple at the kernel level, snappy and beautiful at the surface.

---

## DONE -- Foundation (Phases 1-3)

### Phase 1 -- Gut old stack [COMPLETE]
- Gutted monolithic desktop.c, wm.c, ui_widget.c, ui_event.c
- Stubbed all old apps (filemgr, taskmgr, settings, monitor, finder)
- Kept: gfx.c, gfx_path.c, gfx_ttf.c, ui_theme.c (Catppuccin palette)

### Phase 2 -- Compositor [COMPLETE]
- `compositor.c` (500 LOC): 64-surface pool, 4 layers (wallpaper/windows/overlay/cursor)
- Per-surface damage rects, merged into screen-level dirty rect
- Per-pixel alpha blending with per-surface opacity
- SSE2 non-temporal flip to MMIO framebuffer
- Software cursor as compositor surface (hardware cursor on VirtIO GPU)
- `comp_surface_resize()` for in-place resize without z-order loss

### Phase 3 -- Window Manager [COMPLETE]
- `wm2.c` (700 LOC): 32-window pool, z-order, focus tracking
- macOS-style decorations: 28px title bar, traffic-light buttons, rounded corners
- Full mouse interaction: drag-move, 8-zone resize, button hover/press
- Window states: normal, maximized, minimized
- Canvas API: apps draw to a client buffer, WM blits to compositor surface
- Partial redraw: button hover redraws only button area, not full surface

### Desktop loop [COMPLETE]
- `desktop.c` (250 LOC): non-blocking spin loop
- Mouse routed to wm2, keyboard via `keyboard_getchar_nb()` (non-blocking)
- Bilinear gradient wallpaper on COMP_LAYER_WALLPAPER
- Demo window with FPS counter, repaints once per second
- ESC exits to login (placeholder for power menu)

---

## NEXT -- Phase 4: Desktop Shell

The desktop needs its chrome: a menubar at top, a dock at bottom, and
the infrastructure to launch and manage apps.

### 4.1 Menubar (`menubar.c`)
Compositor surface on COMP_LAYER_OVERLAY, 28px tall, full width.

```
[ Logo ] [ App Name | File  Edit  View ] ............. [ Clock | CPU | Net ]
```

- Left: ImposOS logo button -> power menu (shutdown, restart, lock, logout)
- Center: focused app name + its menu titles (click to open dropdown)
- Right: system tray widgets (clock, CPU %, network status)
- Dropdown menus: popup surfaces on COMP_LAYER_OVERLAY
- Menubar redraws only on: focus change, clock tick, tray data change

### 4.2 Dock (`dock.c`)
Compositor surface on COMP_LAYER_OVERLAY, centered at bottom, floating.

```
                 [Term] [Files] [Settings] [Monitor] | [Trash]
                   .
```

- 48px icons, 8px padding, 12px gap from screen bottom
- Rounded-rect background with slight transparency (alpha=220)
- Separator between pinned apps and running-only apps
- Running indicator: small dot below icon
- Hover: highlight icon (brighter background), tooltip with app name
- Click: launch app or focus existing window
- Phase 4 skip: magnification effect (add in Phase 7)

### 4.3 Wallpaper improvements
- Current bilinear gradient works, keep it
- Add: load a raw pixel image from initrd (BMP or raw ARGB)
- Wallpaper selection in Settings app later

### 4.4 Window management polish
- Alt-Tab switcher: overlay surface showing window thumbnails
- Snap to edges: drag window to screen edge -> half-screen maximize
- Double-click title bar -> maximize toggle (already partially there)

---

## Phase 5 -- App Model

Replace "app = function that loops forever" with an event-driven model
where the desktop loop owns the frame and apps respond to events.

### 5.1 App class

```c
typedef struct app app_t;

typedef struct {
    const char *name;
    const char *icon;           // icon name for dock
    void (*on_create)(app_t *self);
    void (*on_destroy)(app_t *self);
    void (*on_event)(app_t *self, int event_type, int param1, int param2);
    void (*on_paint)(app_t *self, uint32_t *pixels, int w, int h);
    void (*on_resize)(app_t *self, int w, int h);
} app_class_t;

struct app {
    const app_class_t *klass;
    int    win_id;              // wm2 window handle
    void  *state;               // app-private data (malloc'd)
};
```

- `on_paint` called when window needs redraw (damage, resize, focus change)
- `on_event` called for keyboard/mouse events routed by WM to focused app
- Apps never call compositor directly; they paint pixels, WM handles the rest
- App registry: static array of app_class_t pointers, indexed by name

### 5.2 App lifecycle
1. User clicks dock icon (or hotkey)
2. Desktop calls `app_launch("terminal")` -> finds class, creates app_t
3. `app_t` gets a wm2 window, calls `on_create`
4. Desktop loop: drain events -> route to focused app's `on_event`
5. If app marks damage -> call `on_paint` with canvas buffer
6. On close button: call `on_destroy`, free state, destroy window

### 5.3 Built-in apps

| App | Priority | Description |
|-----|----------|-------------|
| Terminal | HIGH | Shell in a window, VT100 emulation, scrollback |
| File Manager | HIGH | Grid/list view, breadcrumb nav, open/copy/delete |
| Settings | MED | Theme picker, keyboard layout, wallpaper, about |
| Task Manager | MED | Process list, CPU/mem bars, kill button |
| Text Editor | MED | Wrap existing vi, add syntax highlighting |
| System Monitor | LOW | Live sparkline graphs (CPU, mem, net) |
| DOOM | LOW | Already works; wrap in app_class, fullscreen mode |

---

## Phase 6 -- Input System Rewrite

The current keyboard system (getchar.c) has a blocking getchar() that
halts the CPU. The desktop already works around this with
keyboard_getchar_nb(), but a proper unified event system is needed.

### 6.1 Unified event queue

```c
typedef struct {
    uint8_t  type;      // KEY_DOWN, KEY_UP, MOUSE_MOVE, MOUSE_BTN, SCROLL
    uint32_t timestamp; // PIT ticks
    union {
        struct { uint8_t scancode; char ch; uint8_t mods; } key;
        struct { int16_t x, y; uint8_t buttons; } mouse;
        struct { int8_t dx, dy; } scroll;
    };
} input_event_t;
```

- Lock-free ring buffer (256 entries), filled by IRQ1 + IRQ12
- Desktop loop drains at frame start
- WM routes: global shortcuts first, then to focused window's app
- Mouse scroll routed to hovered window (not focused) -- like macOS

### 6.2 Keyboard improvements
- Scancode -> character translation extracted into kbd_translate()
- Dead key / compose support for accented chars
- Global hotkeys: Alt+Tab, Ctrl+Space (launcher), Super (overview)

---

## Phase 7 -- Animations & Effects

All animations are purely additive -- nothing depends on them.

- **Window open**: scale up from 80% to 100% + fade in (150ms ease-out)
- **Window close**: scale down to 80% + fade out (100ms ease-in)
- **Minimize**: shrink toward dock icon position (200ms)
- **Alt-Tab**: smooth slide between thumbnails
- **Dock hover**: icon scale 48px -> 64px with neighbor spread (lerp)
- **Notification toast**: slide in from top-right, auto-dismiss (3s)
- **Login crossfade**: keep existing gfx_crossfade()
- Animation engine: `anim_tick(dt_ms)` called at frame start, lerps all active tweens

---

## Constraints

- No MMU process isolation -- kernel and apps share address space
- No GPU shaders -- BGA/VirtIO is a dumb framebuffer, all CPU-side
- No dynamic linking -- apps compiled into kernel binary
- Font: 8x16 bitmap (TTF rasterizer exists for future use)
- Buildable with i686-elf-gcc, no C++, minimal libc

---

## File layout

```
kernel/arch/i386/gui/
  compositor.c      [DONE] scene graph, damage tracking, alpha blit
  wm2.c             [DONE] window manager, decorations, focus, resize
  desktop.c         [DONE] main loop, wallpaper, demo window
  menubar.c         [TODO] top bar: app menu, system tray, clock
  dock.c            [TODO] bottom bar: app icons, launch, indicators
  app.c             [TODO] app lifecycle, registry, launch/destroy
  anim.c            [TODO] animation engine (lerp, easing, timeline)
  gfx.c             [DONE] backbuffer, flip, primitives, cursor
  gfx_path.c        [DONE] vector paths, bezier, scanline fill
  gfx_ttf.c         [DONE] TTF parser, glyph cache, scaled text
  ui_theme.c        [DONE] Catppuccin color tokens, layout metrics
  input.c           [TODO] unified event queue (replace getchar)
  apps/
    terminal.c      [TODO] shell in a window
    filemgr.c       [TODO] file manager
    settings.c      [TODO] settings panel
    taskmgr.c       [TODO] task manager
    doom_app.c      [TODO] DOOM wrapper
```

---

## Implementation order

1. **Menubar** -- static bar with clock + app name (surfaces on COMP_LAYER_OVERLAY)
2. **Dock** -- static icon bar with click-to-launch stubs
3. **App model** -- app_class_t + app_launch() + event routing
4. **Terminal app** -- first real app, shell in a window
5. **File Manager** -- second app, exercises the full stack
6. **Input rewrite** -- unified event queue (current non-blocking approach works for now)
7. **Animations** -- purely additive, last priority
