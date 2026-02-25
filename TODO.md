# ImposOS Desktop — Full GUI Implementation

## Vision

Port the complete HTML desktop mockup into bare-metal i386.
Every feature prototyped in `mockup.html` ships in kernel C:
radial launcher, app drawer, window manager with traffic lights,
5 procedural wallpapers, settings app — all composited, animated, and persisted.

The mockup is the spec. If it works in the browser, it ships on metal.

---

## Status

### DONE — Foundation

| Component | File | LOC | Notes |
|-----------|------|-----|-------|
| Compositor | `compositor.c` | 500 | 64-surface pool, 4 layers, per-pixel alpha, SSE2 flip |
| Window Manager | `wm2.c` | 700 | 32-window pool, decorations, drag, resize, focus |
| Desktop Loop | `desktop.c` | 270 | Space/Tab/Esc shortcuts, wallpaper anim, all overlays wired |
| Graphics | `gfx.c` | — | Backbuffer, flip, primitives, software cursor |
| Vector Paths | `gfx_path.c` | — | Bezier curves, scanline fill |
| TTF Renderer | `gfx_ttf.c` | — | TrueType parser, glyph cache, scaled text |
| Theme Tokens | `ui_theme.c` | — | Catppuccin color palette, layout metrics |
| Wallpaper Engine | `wallpaper.c` | 500 | Mountains/Gradient/Geometric/Stars/Waves × 3-4 themes, animated |
| Animation Engine | `anim.c` | 100 | Integer tweens, linear/ease-in/ease-out/spring |
| Icon Cache | `icon_cache.c` | 200 | Letter avatars + pixel-art symbolic icons |
| App Registry | `app.c` | 180 | 39 apps, 7 categories, pin system (max 8) |
| Radial Launcher | `radial.c` | 320 | Ring of pinned apps, angle hit-test, keyboard nav, type-to-search |
| App Drawer | `drawer.c` | 300 | Full-screen search, category grid, pin/unpin |
| Context Menu | `context_menu.c` | 200 | Right-click desktop, wallpaper/settings/show-minimized items |
| Menubar | `menubar.c` | 220 | Logo→radial, window pills, blue underline, Zeller clock |
| Settings App | `settings.c` | 280 | Wallpaper picker + theme dots + About tab |

---

## Phase 4 — Desktop Shell

### 4.1 Menubar (`menubar.c`)

Surface on `COMP_LAYER_OVERLAY`, 28px tall, full width.
Frosted glass: `rgba(12,16,22,0.72)` + blur, 1px bottom border.

```
[ ImposOS ] [ File  Edit  View ] [ window pills... ] ............. [ Clock ]
```

**Left section:**
- ImposOS logo — bold 14px, click opens radial launcher at screen center
- Tooltip on hover: "App Launcher (Space)"
- First-visit pulse animation (glow 3 cycles)
- File / Edit / View placeholder labels (12.5px, 60% opacity)

**Center section — window pills:**
- Container `menubar-windows` after menu labels
- One pill per open window, created by `updateMenubarWindows()`
- Active pill: white text 85% opacity, `rgba(255,255,255,0.1)` background, blue 2px underline (centered, 50% width)
- Minimized pill: 35% opacity, italic, no underline, `rgba(255,255,255,0.03)` background
- Click pill → restore minimized or bring-to-front
- Hover → brighter background

**Right section:**
- Clock: `weekday, month day  HH:MM` format, updates every second
- Later: CPU %, network status

**Redraw triggers:** focus change, window open/close/minimize, clock tick

### 4.2 Radial Launcher

Canvas-based circular menu, centered on screen.
z-index above overlay (COMP_LAYER_OVERLAY).

**Geometry:**
- Outer radius: 150px (300px at 2x)
- Inner ring radius: 110px (icon orbit)
- Center circle radius: 42px
- Icon size: 46px with 12px corner radius

**Background:**
- Overlay: full-screen `rgba(0,0,0,0.35)`, fade in/out 120ms/180ms
- Ring fill: `rgba(12,22,38,0.78)`, 1.5px white border at 8% opacity
- Open: scale 0.75→1 with spring curve `cubic-bezier(0.34,1.4,0.64,1)` 220ms
- Close: scale 1→0.75 fade out 120ms

**Ring items:**
- Shows pinned apps only (max 8), ordered by `pinnedOrder`
- Items stagger in: 35ms delay per item, 180ms ease-out, scale 0.3→1
- Each item: colored rounded-rect + SVG icon (or 2-letter fallback)
- Slice dividers: `rgba(255,255,255,0.05)` radial lines from center to edge

**Hover/selection:**
- Mouse: hit-test angle + distance → highlight wedge with app color at 18% opacity
- Keyboard: Arrow keys cycle ring, Enter launches
- Center circle shows hovered app name (20px 600wt) or "All apps" (4-dot grid icon + label)
- Center click → opens app drawer

**Launch animation:**
- Icon pulses: scale 1→1.2→1 sine over 220ms
- Wedge flash fades out
- After 220ms: `closeMenu()` + `launchApp(appId)`

**Type-to-search:** any alphanumeric key while radial is open → close radial, open drawer with that character prefilled

### 4.3 App Drawer

Full-screen overlay at z-index 250.
Background: `rgba(0,0,0,0.72)` + blur(40px).

**Layout:**
- Centered container, max-width 860px, padding 48px/36px/60px
- Slide up: `translateY(18px)→0` + fade, 280ms ease-out
- Top: search bar (frosted pill: `rgba(255,255,255,0.07)`, border, 11px radius)
- Below: grid of tiles organized by category

**Search:**
- Placeholder: "Search apps... (Tab)"
- Scoring: exact prefix start=100, substring=60, keyword match=40, category match=20
- Filter results in real-time, show match count

**Tiles:**
- Grid: `auto-fill, minmax(88px, 1fr)`, 6px gap
- Per tile: colored icon (46px, 11px radius) + label (10.5px)
- Hover: `rgba(255,255,255,0.07)` bg, scale 1.06
- Active press: scale 0.95 (60ms)
- Launch: scale 1→1.15→0.6 + fade out (350ms), then close drawer
- Stagger animation: `tileIn` 280ms, 18ms delay per tile

**Categories** (7):
| Category | Color | Icon |
|----------|-------|------|
| System | #3478F6 | gear circle |
| Internet | #5856D6 | globe |
| Media | #FF3B30 | play triangle |
| Graphics | #FF9500 | 3 circles |
| Development | #34C759 | angle brackets |
| Office | #AF52DE | document |
| Games | #00C7BE | gamepad |

**Pin system:**
- Right-click tile → toggle pin (max 8)
- Pinned tiles: blue ring on icon, checkmark badge, brighter label
- Pin count hint: "Right-click to pin · N / 8"
- Max reached → flash red warning (1.2s)
- Drag-and-drop reorder for pinned tiles
- Persist pin order in storage (initrd config or CMOS)

**App catalog** (35+ apps):
- System: Terminal, Files, Settings, Monitor, Disk Usage, System Info, Packages, Users, Logs
- Internet: Browser, Email, Chat, Torrent, FTP Client
- Media: Music, Video Player, Podcasts, Screen Recorder, Image Viewer, Radio
- Graphics: Photo Editor, Vector Draw, Screenshot, Color Picker
- Development: Code Editor, Git Client, Database, API Tester, Debugger
- Office: Writer, Spreadsheet, Presenter, PDF Reader, Notes
- Games: Solitaire, Mines, Chess, Tetris, Snake

### 4.4 Context Menu

Right-click desktop → popup menu.
z-index 300 (above windows).

**Appearance:**
- `rgba(20,28,40,0.88)` + blur(20px), 1px border, 8px radius
- Shadow: `0 8px 32px rgba(0,0,0,0.45)`
- Scale 0.95→1 + fade, 120ms
- Items: 7px/16px padding, 13px text, hover highlight `rgba(52,120,246,0.45)`

**Items:**
- Create Folder, Create File
- (separator)
- Change Wallpaper → opens Settings to wallpaper tab
- Display Settings → opens Settings to display tab
- (separator)
- About ImposOS → opens Settings to about tab
- (separator, if minimized windows exist)
- "↑ Show [AppName]" for each minimized window

### 4.5 Keyboard Shortcuts

| Key | Action |
|-----|--------|
| Space | Toggle radial launcher (center screen) |
| Tab | Toggle app drawer |
| Escape | Close topmost: window → drawer → radial |
| Arrow keys | Navigate radial ring items |
| Enter | Launch highlighted radial item |
| Any letter/digit | Close radial, open drawer with char prefilled |

---

## Phase 5 — Window System Upgrade

Enhance `wm2.c` to match mockup's window manager.

### 5.1 Window Appearance

- Frosted glass: `rgba(18,24,36,0.82)` + blur(32px)
- Border: 1px `rgba(255,255,255,0.1)`
- Shadow: `0 12px 48px rgba(0,0,0,0.5), 0 2px 8px rgba(0,0,0,0.3)`
- Corner radius: 12px
- Min size: 320×200

### 5.2 Titlebar (38px)

- Background: `rgba(255,255,255,0.04)`, 1px bottom border
- Grab cursor for drag
- Title: 12.5px, 65% opacity, centered (offset right by 46px for balance)

**Traffic lights** (left side, 7px gap):
- Close: #ff5f57 (red) — "×" on hover
- Minimize: #ffbd2e (yellow) — "−" on hover
- Maximize: #28c840 (green) — "⤢" on hover
- All 12px circles, symbols appear on parent hover, brightness 1.15 on hover

### 5.3 Window States

**Normal:** positioned, resizable, draggable
**Fullscreen:** top=28px, left=0, width=100%, height=100%−28px, no border-radius, no border, resize handles hidden
**Minimized:** opacity 0, pointer-events none, fly-to-pill animation

### 5.4 Minimize Animation

**Minimize (fly to pill):**
1. Set `entry.minimized = true`
2. Update menubar pills (so target pill exists)
3. Compute pill's bounding rect
4. Set window transform: `translate(targetX, targetY) scale(0.08)` — target = pill center minus window center
5. CSS transition: opacity 0.3s ease-in, transform 0.35s `cubic-bezier(0.4,0,0.8,0.4)`
6. Add minimized class (opacity 0, pointer-events none)

**Restore (fly from pill):**
1. Set `entry.minimized = false`
2. Override transition: opacity 0.25s ease-out, transform 0.3s `cubic-bezier(0.2,0.8,0.3,1)`
3. Clear transform → animates from pill position back to original
4. Remove minimized class, bring to front
5. After 350ms, clear transition override

### 5.5 Open/Close Animations

**Open:** scale 0.88→1 + fade in, 220ms spring curve
**Close:** scale 1→0.92 + fade out, 140ms ease-in, then remove from DOM

### 5.6 Resize System

8 handles around window edges/corners:
- 4 edges: 6px wide, inset 8px from corners, appropriate cursor
- 4 corners: 12×12px at corners, diagonal cursors
- Hidden when fullscreen
- Min size enforced: 320×200

### 5.7 Drag Clamping

- Horizontal: at least 60px of window visible
- Vertical: top ≥ 28px (below menubar), bottom ≥ 38px reachable

### 5.8 Single Instance + Focus

- `openWindows[]` array tracks all windows by appId
- Opening same app: if minimized → restore; if visible → bring to front
- Bring to front: set z-index to max+1
- Click anywhere on window → bring to front

### 5.9 Window Restore Paths

Three ways to restore a minimized window:
1. **Click menubar pill** → `restoreAppWindow()`
2. **Re-launch same app** (radial/drawer) → auto-restore
3. **Right-click desktop** → "↑ Show [AppName]" context menu item

---

## Phase 6 — Wallpaper Engine

### 6.1 Architecture

Replace static gradient wallpaper with procedural engine.

```c
typedef struct {
    const char *id;
    const char *label;
    wallpaper_theme_t *themes;
    int theme_count;
    void (*draw)(uint32_t *buf, int w, int h,
                 wallpaper_theme_t *thA, wallpaper_theme_t *thB, float t);
} wallpaper_style_t;
```

**Dispatch system:**
- `WALLPAPER_STYLES[]` registry array (5 styles)
- `cur_style_id`, `cur_theme_idx` state
- `set_wallpaper_style(id, theme_idx)` — snap switch
- `set_theme(idx)` — cross-fade within style (ease 0→1 at +0.008/frame)
- `draw_current_wallpaper()` — dispatches to active style's draw function
- Each draw function works at any resolution (fullscreen + thumbnails)

### 6.2 Wallpaper Styles

| Style | Draw Function | Description |
|-------|--------------|-------------|
| Mountains | `draw_mountains()` | Sky gradient, 3 mountain layers, aurora, stars, glow |
| Gradient | `draw_gradient()` | Multi-stop linear gradient with drifting direction + 3 soft orbs |
| Geometric | `draw_geometric()` | Tessellated triangles in grid, pulsing opacity |
| Stars | `draw_stars()` | Deep space background + nebula radial clouds + 200 deterministic stars |
| Waves | `draw_waves()` | Sky gradient + 5 sine-wave layers with compound oscillation |

### 6.3 Theme Variants

**Mountains** (4):
- Night: dark sky, bright stars, aurora
- Dawn: warm orange/purple sky, faint stars
- Day: blue sky, no stars, no aurora
- Dusk: red/orange sky, faint stars

**Gradient** (4):
- Sunset: purple→red→orange→gold
- Ocean: navy→blue→cyan
- Aurora: dark→teal→green
- Midnight: dark purple→violet

**Geometric** (3):
- Dark: muted blue-gray triangles
- Colorful: vibrant purple/red/orange/green/blue
- Neon: hot pink/green/blue/orange/purple on near-black

**Stars** (3):
- Deep Space: sparse nebula, seed 42
- Nebula: dense purple/blue/red clouds, seed 77
- Starfield: minimal nebula, clean stars, seed 123

**Waves** (3):
- Ocean: blue sky, blue-teal waves
- Sunset Sea: warm sky, dark red waves
- Arctic: gray sky, muted teal waves

### 6.4 Animation

- Mountains: star flicker (sin wave per star, offset by brightness seed)
- Gradient: gradient angle drifts slowly, orbs orbit gently
- Geometric: triangle opacity pulses with sin(time + position)
- Stars: each star flickers independently, nebula clouds drift
- Waves: wave layers oscillate with compound sine functions at different speeds
- All use `Date.now()` (or PIT ticks) for time-based animation, no frame-count dependency

### 6.5 Persistence

- Store `{ style_id, theme_idx }` in persistent storage (CMOS, or config file in initrd)
- Load at boot before first frame
- Save on every style/theme change

---

## Phase 7 — Settings App

First real application built on the app model.

### 7.1 Settings Window Shell

- Opens via `launchApp("settings")` or context menu shortcuts
- Window size: 680×440
- Layout: sidebar (170px) + content pane
- Sidebar: frosted dark background, 1px right border

**Tabs:**
| Tab | Icon | Status |
|-----|------|--------|
| Wallpaper | landscape icon | Functional |
| Appearance | circle+cross | Placeholder "Coming soon" |
| Display | monitor icon | Placeholder "Coming soon" |
| About | info circle | Shows version info |

- Active tab: blue highlight `rgba(52,120,246,0.3)`, white text
- Click tab → swap content pane
- Can be opened to specific tab: `openSettingsWindow('wallpaper')`

### 7.2 Wallpaper Settings UI

**Thumbnail grid:**
- 5 cards in a flex row, 140px wide, 16:10 aspect ratio (140×87 canvas)
- Each renders a static preview using the style's draw function at small resolution
- Active style: blue border `rgba(52,120,246,0.8)`
- Hover: scale 1.04, brighter border
- Click → `set_wallpaper_style()` → wallpaper changes immediately, rebuild UI

**Variant dots (below grid):**
- Row of 20px circles, filled with theme's representative color
- Active variant: white border
- Hover: scale 1.15, brighter border
- "Theme:" label prefix, current variant name suffix
- Click dot → switch theme within current style

### 7.3 About Tab

- Large "ImposOS" text (28px bold)
- "Version 0.1" below
- "A concept desktop environment" subtitle (30% opacity)

---

## Phase 8 — App Model

### 8.1 App Class

```c
typedef struct app app_t;

typedef struct {
    const char *name;
    const char *id;           // unique string id
    const char *icon;         // icon identifier
    uint32_t   color;         // ARGB icon background color
    const char *category;     // category id
    const char **keywords;    // search keywords (NULL-terminated)
    bool       default_pin;   // pinned by default
    void (*on_create)(app_t *self);
    void (*on_destroy)(app_t *self);
    void (*on_event)(app_t *self, int event_type, int p1, int p2);
    void (*on_paint)(app_t *self, uint32_t *pixels, int w, int h);
    void (*on_resize)(app_t *self, int w, int h);
} app_class_t;

struct app {
    const app_class_t *klass;
    int    win_id;
    void  *state;
};
```

### 8.2 App Lifecycle

1. User action (radial/drawer/hotkey) → `app_launch("terminal")`
2. Find class in registry, create `app_t`, allocate window
3. Call `on_create` → app sets up state
4. Desktop loop: drain events → route to focused app's `on_event`
5. If app marks damage → call `on_paint` with canvas buffer
6. On close: call `on_destroy`, free state, destroy window

### 8.3 Launch Dispatcher

```c
void launch_app(const char *app_id) {
    if (strcmp(app_id, "settings") == 0) { open_settings_window(); return; }
    // ... other real apps ...
    // Placeholder: log to console
}
```

All launch points call this dispatcher:
- Radial launcher → `launchItem()` → `launch_app(id)`
- App drawer → tile click → `launch_app(id)`
- Context menu → "Change Wallpaper" → `launch_app("settings")`

---

## Phase 9 — Input System

### 9.1 Unified Event Queue

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
- WM routes: global shortcuts first → focused window's app
- Mouse scroll → hovered window (not focused), like macOS

### 9.2 Keyboard Improvements

- Scancode → character translation in `kbd_translate()`
- Global hotkeys: Space (radial), Tab (drawer), Escape (close topmost)
- Arrow key navigation in radial
- Type-to-search from radial to drawer

---

## Phase 10 — Built-in Apps

| App | Priority | Description |
|-----|----------|-------------|
| Terminal | HIGH | VT100 emulation, scrollback, shell in a window |
| File Manager | HIGH | Grid/list view, breadcrumb nav, open/copy/delete |
| Settings | DONE | Wallpaper picker, about (appearance/display placeholders) |
| Task Manager | MED | Process list, CPU/mem bars, kill button |
| Text Editor | MED | Existing vi wrapped in window, syntax highlighting |
| System Monitor | LOW | Sparkline graphs (CPU, memory, network) |
| DOOM | LOW | Existing port wrapped in app_class, fullscreen mode |

---

## Phase 11 — Animations

All purely additive — nothing depends on them.

| Animation | Duration | Easing |
|-----------|----------|--------|
| Window open | 220ms | scale 0.88→1, spring `(0.34,1.3,0.64,1)` |
| Window close | 140ms | scale 1→0.92, ease-in |
| Minimize (to pill) | 350ms | translate+scale(0.08), `(0.4,0,0.8,0.4)` |
| Restore (from pill) | 300ms | translate back, `(0.2,0.8,0.3,1)` |
| Radial open | 220ms | scale 0.75→1, spring |
| Radial close | 120ms | scale 1→0.75, ease-in |
| Drawer slide | 280ms | translateY(18→0), ease-out |
| Tile stagger | 280ms | 18ms per tile, translateY(10→0)+scale(0.92→1) |
| Tile launch | 350ms | scale 1→1.15→0.6 + fade |
| Context menu | 120ms | scale 0.95→1, ease-out |
| Logo pulse | 2s × 3 | text-shadow glow cycles on first visit |
| Star flicker | continuous | per-star sin oscillation |
| Wallpaper cross-fade | ~125 frames | eased interpolation between themes |

Animation engine: `anim_tick(dt_ms)` called at frame start, lerps all active tweens.

---

## Phase 12 — SVG Icon System

### 12.1 Icon Registry

20 SVG icons pre-rasterized into bitmap cache at 26×26 (or 2x):

| Icon ID | Description |
|---------|-------------|
| terminal | `>_` prompt lines |
| files | folder outline |
| browser | globe with meridians |
| music | note with arc |
| settings | gear + inner circle |
| monitor | screen on stand |
| email | envelope with chevron |
| chat | speech bubble |
| video | camera + viewfinder |
| code | angle brackets + slash |
| image | frame with circle + landscape |
| pdf | document with fold |
| gamepad | controller |
| disk | CD/record |
| users | 2 people silhouettes |
| download | arrow down + line |
| table | grid |
| pen | pencil on paper |
| box | 3D cube wireframe |
| (7 category icons) | system, internet, media, graphics, dev, office, games |

### 12.2 Rendering

- Parse SVG paths at compile time or init → rasterize to `uint32_t` bitmaps
- Cache per icon per size (26px for radial, 24px for drawer tiles, small for dock)
- Letter avatar fallback: 2-letter abbreviation in bold white on colored rect
- Icon tinting: white strokes on transparent, composited over app color background

---

## Constraints

- No MMU process isolation — kernel and apps share address space
- No GPU shaders — BGA/VirtIO is a dumb framebuffer, all CPU-side
- No dynamic linking — apps compiled into kernel binary
- Font: 8×16 bitmap (TTF rasterizer exists for future use)
- Buildable with i686-elf-gcc, no C++, minimal libc
- Mockup = spec: if it works in the browser, it ships on metal

---

## File Layout

```
kernel/arch/i386/gui/
  compositor.c      [DONE]  Scene graph, damage tracking, alpha blit
  wm2.c             [DONE]  Window manager (upgrade in Phase 5)
  desktop.c          [DONE]  Main loop (upgrade in Phases 4, 5)
  menubar.c          [TODO]  Top bar: logo, menu labels, window pills, clock
  radial.c           [TODO]  Radial launcher: canvas ring, hit testing, animation
  drawer.c           [TODO]  App drawer: search, categories, tiles, pin system
  context_menu.c     [TODO]  Right-click menus
  wallpaper.c        [TODO]  5 procedural wallpaper styles + theme engine
  app.c              [TODO]  App lifecycle, registry, launch dispatcher
  icon_cache.c       [TODO]  SVG→bitmap rasterizer, icon registry
  anim.c             [TODO]  Animation engine (lerp, easing, timeline)
  gfx.c              [DONE]  Backbuffer, flip, primitives, cursor
  gfx_path.c         [DONE]  Vector paths, bezier, scanline fill
  gfx_ttf.c          [DONE]  TTF parser, glyph cache, scaled text
  ui_theme.c         [DONE]  Color tokens, layout metrics
  input.c            [TODO]  Unified event queue (replace getchar)
  apps/
    settings.c       [TODO]  Settings panel (wallpaper picker, about)
    terminal.c       [TODO]  Shell in a window
    filemgr.c        [TODO]  File manager
    taskmgr.c        [TODO]  Task manager
    doom_app.c       [TODO]  DOOM wrapper
```

---

## Implementation Order

1. **Input system** — unified event queue (foundation for everything)
2. **Menubar** — static bar with clock + logo click handler
3. **Wallpaper engine** — 5 procedural styles + theme cross-fade
4. **Icon cache** — SVG rasterizer, letter fallbacks
5. **Context menu** — popup rendering + hit testing
6. **Window system upgrade** — traffic lights, minimize/restore, fullscreen, resize, animations
7. **App model** — app_class_t + launch dispatcher
8. **Settings app** — wallpaper picker + about tab
9. **Radial launcher** — ring rendering, hit testing, keyboard nav
10. **App drawer** — search, categories, tiles, pin/unpin, drag reorder
11. **Menubar window pills** — integrate with window manager
12. **Terminal app** — first real app
13. **File manager** — second real app
14. **Animations** — polish pass (stagger, springs, fly-to-pill)

---

## Verification Checklist

- [ ] Space → radial opens centered, shows pinned apps in ring
- [ ] Click radial slice → app launches, radial closes with animation
- [ ] Arrow keys navigate ring, Enter launches
- [ ] Center click → app drawer opens
- [ ] Tab → drawer opens with search focused
- [ ] Type in drawer → apps filter in real-time
- [ ] Click drawer tile → launch animation, drawer closes, app opens
- [ ] Right-click tile → pin/unpin toggle (max 8 enforced)
- [ ] Drag pinned tiles to reorder
- [ ] Right-click desktop → context menu appears
- [ ] "Change Wallpaper" → Settings opens to wallpaper tab
- [ ] Settings window: traffic lights visible (close/minimize/maximize)
- [ ] Close button → window shrinks + fades out
- [ ] Minimize button → window flies to menubar pill position
- [ ] Click pill → window flies back from pill position
- [ ] Maximize button → window fills screen below menubar
- [ ] Double-click titlebar → toggle fullscreen
- [ ] Drag titlebar → window follows, clamped to screen
- [ ] Drag window edge/corner → resize, min 320×200
- [ ] Wallpaper grid shows 5 thumbnail previews
- [ ] Click different wallpaper → desktop changes immediately
- [ ] Click variant dot → theme changes within style
- [ ] Reload → wallpaper choice persists
- [ ] All 5 wallpapers animate continuously (stars flicker, waves move, etc.)
- [ ] Escape closes topmost window, then drawer, then radial
- [ ] Re-opening already-open app restores minimized or brings to front
- [ ] Context menu shows "↑ Show [App]" for minimized windows
- [ ] Menubar pills update in real-time as windows open/close/minimize
