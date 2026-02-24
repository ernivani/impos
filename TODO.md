# ImposOS Desktop Rewrite — Modern GUI Architecture

## Vision

Tear out the current ad-hoc desktop/WM/widget stack and replace it with a
clean, layered rendering architecture capable of sustained high FPS on bare
metal i386. Inspiration: macOS (compositing model, dock, menubar), KDE Plasma
(panel flexibility, effects), GNOME (activity overview, app model). The goal
is to leapfrog all three by keeping the design radically simple at the kernel
level while delivering a snappy, beautiful result.

---

## Phase 1 — Nuke the old stack

### Files to delete / gut
- `desktop.c` — monolithic 3000-line file mixing layout, drawing, input, state
- `wm.c` — shadow atlas, bg_cache, composite logic tightly coupled to desktop
- `ui_widget.c` — 8-window hard cap, no layout engine, pixel-perfect hacks
- `ui_theme.c` — keep color tokens, remove all sizing magic numbers
- `ui_event.c` — replace with a proper event queue (not a flag + callback)
- `login.c` — rebuild as a first-class app, not a special boot state
- `filemgr.c`, `finder.c`, `taskmgr.c`, `monitor.c`, `settings.c` — rebuild
  as apps on top of the new stack, not one-off drawing routines

### What to keep
- `gfx.c` — backbuffer, `gfx_flip()`, SSE2 `memcpy_nt`, BGA init — solid
- `gfx_path.c` — vector primitives (rect, circle, line) — keep and extend
- `gfx_ttf.c` — font rasterizer — keep, add kerning + subpixel hints
- `ui_theme.c` color tokens (Catppuccin palette is good) — keep values, gut
  the rest

---

## Phase 2 — New rendering pipeline

### 2.1 Retained-mode scene graph (`compositor.c`)
Replace the immediate-mode draw-everything-every-frame approach with a
retained scene graph:

```
Scene
 └── Layer[]          (z-ordered: wallpaper, windows, overlay, cursor)
      └── Surface[]   (opaque region + damage rect + pixel buffer)
```

- Each `Surface` owns a pixel buffer (ARGB 32-bit)
- Compositor only repaints surfaces whose `damage` rect is non-zero
- Final blit to backbuffer only copies changed scanline spans → cuts
  memory bandwidth by 60-80% on typical workloads
- `gfx_flip()` stays as-is (SSE2 blit to framebuffer)

### 2.2 Damage tracking
- Every draw call marks a dirty rect on its surface
- WM merge-clips dirty rects per frame before composite
- Skip full-screen redraws; only composite what changed

### 2.3 Frame pacing
- PIT at 1000 Hz gives 1 ms tick resolution
- Target: 60 fps (16 ms budget), cap at 120 fps on fast paths
- Frame loop: `input → update → damage-composite → flip → sleep remainder`
- Remove the current `WM_FRAME_INTERVAL` polling hack

---

## Phase 3 — Window manager rewrite (`wm2.c`)

### Core model (inspired by Wayland/KWin internals, simplified)
```c
typedef struct {
    int      id;
    rect_t   geom;          // x, y, w, h
    rect_t   damage;        // dirty region this frame
    uint32_t *pixels;       // client-owned surface buffer
    char     title[64];
    uint8_t  alpha;         // window opacity 0-255
    int      state;         // NORMAL | MAXIMIZED | MINIMIZED | FULLSCREEN
    int      z;             // z-index
} wm_client_t;
```

- No hard window count cap (dynamic array, heap-allocated)
- Window decorations drawn by WM into a separate decoration surface,
  not mixed into client pixels (clean separation like KWin)
- Rounded corners via pre-computed alpha mask, applied at composite time
- Drop shadow rendered once into a shadow surface, cached until resize

### Title bar
- macOS-style: traffic-light buttons (close/min/max) on left
- Double-click title bar → maximize (toggle)
- Middle-click title bar → shade (roll up) — KDE feature
- Right-click title bar → window menu (move, resize, always-on-top, close)

### Resize
- Invisible 4px resize border on all edges + corners (8 handles)
- Live resize: redraw client every frame during drag (not just outline)
- Snap to screen edges and other windows (magnetism like KDE)

### Focus model
- Click-to-focus with optional sloppy focus (hover = focus)
- Focus ring drawn by WM at composite time (1px accent-colored border)

---

## Phase 4 — Desktop shell rewrite (`shell2/`)

### Layout (macOS + GNOME hybrid)

```
┌─────────────────────────────────────────┐
│  Menubar  [App menu] [File Edit …]  [Clock CPU Network] │  ← 24px
├─────────────────────────────────────────┤
│                                         │
│              Wallpaper                  │
│         (desktop icon grid)             │
│                                         │
├─────────────────────────────────────────┤
│  [Icon][Icon][Icon][Icon]  ○○○  [Icon]  │  ← Dock 60px
└─────────────────────────────────────────┘
```

### Menubar (`menubar.c`)
- 24px bar, always on top (own compositor layer)
- Left: Apple-logo menu → About, Settings, Shutdown
- Center: active app name + app menu (File, Edit, View, …)
- Right: system tray — clock, CPU bar, network indicator, volume
- Menus are popup surfaces, not modal loops

### Dock (`dock.c`)
- Centered at bottom, floating (gap between dock and screen edge)
- App icons: 48×48 SVG-like pixel art or pre-rasterized bitmaps
- Hover magnification: icon scales to 72px with smooth lerp (60fps)
- Running indicator: small dot below icon
- Drag-to-reorder, drag-off-to-remove
- App badge overlay (notification count)

### Activity overview (GNOME-inspired, `overview.c`)
- Triggered by Super key or hot corner (top-left)
- All windows scale down and spread across screen
- Search bar at top filters running apps + installed apps
- Click window thumbnail to focus it

### Wallpaper engine (`wallpaper.c`)
- Static color, gradient (bilinear, 4-corner), or pixel art image
- Smooth animated gradient option (slow color shift, ~0.5fps update)
- Wallpaper drawn once to a locked surface; never recomposited unless changed

### Desktop icons (`icon_grid.c`)
- Uniform grid layout (auto-arrange option)
- Single-click selects, double-click opens
- Rubber-band multi-select
- Right-click context menu

---

## Phase 5 — Input system rewrite (`input.c`)

Replace the current `getchar()` polling + IRQ flag approach:

### Event queue
```c
typedef struct {
    uint8_t  type;     // KEY_PRESS | KEY_RELEASE | MOUSE_MOVE | MOUSE_BTN | SCROLL
    uint32_t time_ms;
    union {
        struct { uint8_t scancode; uint32_t codepoint; uint8_t mods; } key;
        struct { int16_t dx, dy; int16_t ax, ay; uint8_t buttons; } mouse;
        struct { int16_t dx, dy; } scroll;
    };
} input_event_t;
```

- Ring buffer of 256 events, filled by IRQ handlers (keyboard IRQ1, mouse IRQ12)
- WM drains queue at frame start, routes events to focused window or global shortcuts
- No more `getchar()` blocking the frame loop

### Keyboard
- Decouple scancode → Unicode from the WM; make it a small `kbd_translate()` call
- Dead key support for compose sequences (é, ü, ñ …)
- Global shortcuts handled by WM before routing to apps

### Mouse
- Absolute position tracking (not just deltas)
- Hardware cursor: write cursor pixels directly to framebuffer after composite,
  save/restore background — already done, keep this approach
- Scroll wheel events routed to hovered window (not focused) — like macOS/KDE

---

## Phase 6 — App model (`app.c`)

Replace the current "app = a function that loops forever" model:

### App lifecycle
```c
typedef struct {
    const char *name;
    void (*init)(app_t *self);
    void (*on_event)(app_t *self, input_event_t *ev);
    void (*on_paint)(app_t *self, uint32_t *pixels, int w, int h);
    void (*on_close)(app_t *self);
} app_class_t;
```

- `on_paint` called only when window is damaged
- Apps do NOT call `gfx_*` directly; they receive a pixel buffer to draw into
- WM owns the window surface; app writes to it via `on_paint` callback
- Clean separation: WM composites, app paints — no coupling

### Built-in apps to rewrite on the new model
| App | Notes |
|-----|-------|
| Terminal | Shell in a window; uses existing shell_loop logic |
| File Manager | Grid + list view, breadcrumb path bar |
| Settings | Category sidebar + content pane |
| Task Manager | Live CPU/mem graphs, kill button |
| Text Editor (vi) | Already exists; wrap in app_class |
| System Monitor | Sparkline graphs, per-process rows |
| DOOM | Fullscreen app; bypasses compositor for direct framebuffer access |

---

## Phase 7 — Animations & effects

- **Window open/close**: scale from dock icon position to final size (200ms ease-out)
- **Expose / overview**: smooth scale + fade (300ms)
- **Notification toasts**: slide in from top-right, auto-dismiss (3s)
- **Dock magnification**: per-icon scale lerp at 60fps, no realloc
- **Crossfade on login**: keep existing `gfx_crossfade()` — it works
- All animations driven by a global `anim_tick(dt_ms)` called at frame start

---

## Constraints & non-goals

- **No MMU per-process isolation** — kernel and apps share address space (bare metal reality)
- **No GPU shaders** — BGA is a dumb framebuffer; all effects are CPU-side
- **No dynamic linking** — apps compiled into the kernel binary for now
- **No anti-aliasing on fonts** — 8×16 bitmap font; add subpixel rendering later
- **Keep it buildable with i686-elf-gcc** — no C++ features, no libc

---

## File layout after rewrite

```
kernel/arch/i386/gui/
  compositor.c      ← scene graph, damage tracking, blit
  wm2.c             ← window manager (client list, decorations, focus)
  input.c           ← event queue, keyboard translate, mouse
  desktop_shell.c   ← menubar, dock, overview, wallpaper, icon grid
  app.c             ← app lifecycle, registry
  anim.c            ← animation engine (lerp, easing, timeline)
  gfx.c             ← keep (backbuffer, flip, primitives)
  gfx_path.c        ← keep
  gfx_ttf.c         ← keep
  ui_theme.c        ← keep color tokens only
  apps/
    terminal.c
    filemgr.c
    settings.c
    taskmgr.c
    doom_app.c
```

---

## Implementation order

1. `compositor.c` + damage rects (can test standalone with a moving rect)
2. `input.c` event queue (replaces getchar polling — immediate win)
3. `wm2.c` basic window stack on top of compositor
4. `desktop_shell.c` menubar + dock (static, no animations yet)
5. Port apps one by one to `app_class_t` model
6. Add animations last (purely additive, not load-bearing)
