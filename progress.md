# ImposOS — Progress & Future Roadmap

> Last updated: February 2026
> Status: **Core OS complete + ring 3 + IPC pipes** | ~28,200 lines of code | ~190 regression tests | 42+ shell commands

---

## Table of Contents

1. [Project Overview](#project-overview)
2. [What's Done (Complete Features)](#whats-done)
3. [What's Partially Done](#whats-partially-done)
4. [What's Missing](#whats-missing)
5. [Known Limitations](#known-limitations)
6. [Future TODO List](#future-todo-list)
7. [Architecture Summary](#architecture-summary)
8. [File Map](#file-map)
9. [Stats](#stats)

---

## Project Overview

ImposOS is a bare-metal i386 operating system built from scratch. It boots via GRUB (Multiboot), runs in 1920x1080x32 graphical mode, and provides a full desktop environment with window manager, networking stack, file system, multi-user authentication, and a 41-command shell.

**Quick start:**
```bash
make run    # Build + boot in QEMU
```

---

## What's Done

### Boot & Kernel (Phase 1) — 100%
- [x] Multiboot-compliant boot (magic 0x1BADB002)
- [x] Linker script placing kernel at 1 MiB
- [x] Constructor/destructor support (crti.S/crtn.S)
- [x] GDT: 3 entries (null, code ring 0, data ring 0)
- [x] IDT: 256 entries for exceptions + IRQs
- [x] PIC (8259A): Master + slave, IRQ 0-15 mapped to INT 32-47
- [x] PIT (8254): 100 Hz timer, `pit_get_ticks()`, `pit_sleep_ms()`
- [x] ISR stubs for all 32 CPU exceptions + 16 hardware IRQs
- [x] GRUB ISO generation, boots in QEMU and VirtualBox

### Display (Phase 2) — 100%
- [x] VGA text mode: 80x25, 16 colors, scroll, hardware cursor
- [x] VBE/VESA graphical mode: 1920x1080, 32bpp framebuffer
- [x] Double buffering (backbuffer in RAM, flip to framebuffer)
- [x] Graphics primitives: pixels, filled/outlined rects, lines (Bresenham), circles
- [x] Alpha blending: `gfx_blend_pixel()`, `gfx_fill_rect_alpha()`, `gfx_overlay_darken()`
- [x] Bitmap font (8x16, 256 glyphs), `gfx_draw_char()` / `gfx_draw_string()`
- [x] Animations: crossfade, smooth transitions between screens

### libc (Phase 3) — 100%
- [x] **string.h**: memcmp, memcpy, memmove, memset, memchr, strlen, strnlen, strcpy, strncpy, strcat, strcmp, strncmp, strtok, strchr, strrchr, strstr, strdup, strndup, strcspn, strspn, strpbrk
- [x] **stdio.h**: printf, snprintf, sprintf, vsnprintf, putchar, getchar, puts, fopen, fclose, fread, fwrite, fgetc, fputc, fflush, feof, ferror, fputs, fgets, fprintf, sscanf, fscanf
- [x] **stdlib.h**: malloc, free, realloc, calloc, atoi, strtol, abort, exit, abs, labs, div, ldiv, rand, srand, qsort, bsearch
- [x] **setjmp.h**: setjmp/longjmp (assembly)
- [x] **stdarg.h**: va_list, va_start, va_end, va_arg (GCC builtins)

### Keyboard (Phase 4) — 100%
- [x] IRQ1 interrupt-driven keyboard with ring buffer
- [x] Two layouts: FR AZERTY and US QWERTY (switchable via `setlayout`)
- [x] Special keys: arrows, Home, End, Delete, Alt+Tab
- [x] Line editing: Ctrl+U, Ctrl+K, Ctrl+W, Ctrl+A, Ctrl+E, Ctrl+L, Ctrl+C
- [x] Idle callback for mouse polling during keyboard waits

### Mouse (Phase 4b) — 100%
- [x] PS/2 3-byte packet protocol (IRQ12)
- [x] Cursor tracking on framebuffer
- [x] Double-click detection (500ms window)
- [x] Left/right/middle button support

### Disk I/O (Phase 5) — 100%
- [x] ATA driver: LBA28 addressing, primary IDE channel (0x1F0)
- [x] Read/write sectors, flush cache
- [x] BSY/DRQ polling, error detection
- [x] 10 MB persistent disk image (auto-created on first run)

### File System (Phase 6) — 100%
- [x] Custom on-disk format: 64 inodes, 256 blocks (512B each)
- [x] Inode structure: type, mode (rwx), uid, gid, size, blocks, indirect block (48 bytes packed)
- [x] 8 direct blocks (4 KB) + single-indirect block (128 pointers, ~64 KB) = max ~69 KB per file
- [x] Directories with . and .. entries, 28-char filenames
- [x] Absolute/relative path parsing
- [x] Full CRUD: create, read, write, delete files and directories
- [x] Symlinks: stored as special inode type, resolved at path lookup
- [x] Permissions: rwx bits for owner/group/other, chmod/chown
- [x] Ownership: uid/gid per file, enforced on access
- [x] Per-user quotas: inode + block limits (/etc/quota)
- [x] Persistence: `fs_sync()` writes to disk, validated on load
- [x] I/O statistics tracking

### Shell (Phase 7) — 100%
- [x] 41+ commands (see full list in [Architecture Summary](#architecture-summary))
- [x] Command history (ring buffer, arrow key navigation)
- [x] Tab completion (commands + filenames)
- [x] Configurable prompt (PS1 with \w expansion, colored segments)
- [x] Foreground app system (top, monitor run as foreground processes)

### Text Editor (Phase 8) — 100%
- [x] vi modal editor: command mode + insert mode
- [x] Open, edit, save, quit
- [x] Works in both VGA text mode and graphical mode
- [x] Vi-style movement (h/j/k/l + arrow keys)

### Networking (Phase 9) — 100%
- [x] **L2 — Ethernet**: RTL8139 (QEMU) + PCnet-FAST III (VirtualBox), auto-detected
- [x] **L3 — IP**: IPv4 header parsing, checksum, TTL, protocol demux
- [x] **L3 — ARP**: Request/response, 16-entry cache with 5-min timeout
- [x] **L3 — ICMP**: Echo request/reply (ping)
- [x] **L4 — UDP**: 8 bindings, ring buffer per port, checksum
- [x] **L4 — TCP**: 8 connections, full state machine (SYN/ESTABLISHED/FIN), 3-way handshake, 4 KB TX/RX buffers, retransmission, MSS=1400
- [x] **Socket API**: 16 sockets, SOCK_STREAM + SOCK_DGRAM
- [x] **DNS client**: Type A queries over UDP port 53
- [x] **DHCP client**: Full DORA (Discover/Offer/Request/Ack), auto-configures IP/netmask/gateway
- [x] **HTTP server**: HTTP/1.0 on port 80, responds with HTML
- [x] **Firewall**: 16 rules, IP+port+protocol filtering, first-match-wins, default action

### Users & Authentication (Phase 10) — 100%
- [x] /etc/passwd with salt+hash (PBKDF2-style, 10K iterations)
- [x] /etc/group with group membership
- [x] Max 16 users, max 16 groups
- [x] First-boot setup wizard (hostname, root user, standard user)
- [x] Login: both GUI and text mode
- [x] Session management: current user, HOME, USER, PS1 env vars
- [x] Commands: whoami, su, id, useradd, userdel

### Configuration (Phase 11) — 100%
- [x] /etc/config: keyboard layout, date/time, timezone, PS1, format prefs
- [x] /etc/hostname: persistent hostname
- [x] Environment variables: env_get/env_set/env_list, export command
- [x] Shell history persistence

### Desktop Environment (Phase 14) — 100%
- [x] State machine: boot -> splash -> login -> desktop
- [x] Animated splash screen with gradient fade-in
- [x] GUI login with username/password fields
- [x] Gradient wallpaper with color interpolation
- [x] Bottom dock: 7 app icons (folder, terminal, finder, settings, monitor, taskmgr, app store)
- [x] Hover effects, selection states, double-click to launch
- [x] Real-time clock display

### Window Manager — 100%
- [x] 32 concurrent windows with Z-order
- [x] Dragging with throttling (~30 fps)
- [x] Edge resize (6px zones)
- [x] Minimize/maximize/restore/close buttons
- [x] Titlebar (28px) with hover effects
- [x] Per-window canvas (pixel buffer)
- [x] Dirty flag tracking, background caching
- [x] Composite rendering in Z-order

### Widget Toolkit — 100%
- [x] 12 widget types: Label, Button, TextInput, List, Checkbox, Progress, Tabs, Panel, Separator, Custom, Toggle, IconGrid, Card
- [x] Layout system with parent-child relationships
- [x] Event handling: keyboard/mouse, focus management
- [x] Callbacks: on_click, on_change, on_submit
- [x] Max 48 widgets per window

### GUI Applications — 100%
- [x] **File Manager**: browsing, navigation, create/delete/rename, list + icon view modes
- [x] **Task Manager**: CPU usage bars, task list with sorting, kill support
- [x] **Settings**: 5 tabs (network, user prefs, system info, etc.)
- [x] **Resource Monitor**: CPU trending, memory stats, I/O stats, network throughput
- [x] **Finder**: fuzzy file search, quick launch (Alt+Space)
- [x] **Login GUI**: username/password, first-boot setup wizard

### Theme System — 100%
- [x] Catppuccin-inspired dark theme
- [x] 40+ color values (semantic: success, danger, accent, warning)
- [x] Layout parameters: padding, spacing, border radius, heights

### ACPI — 100%
- [x] RSDP discovery (BDA + EBDA scan)
- [x] RSDT/FADT/DSDT parsing
- [x] S5 sleep state extraction
- [x] Clean shutdown via `shutdown` command

### Task Tracking & Preemptive Multitasking — 100%
- [x] 32 task slots (4 fixed cooperative: idle, kernel, wm, shell; 28 preemptive)
- [x] Per-task CPU accounting (PIT-driven tick counting)
- [x] CPU % calculation per second
- [x] Hog detection watchdog (>90% CPU for consecutive seconds)
- [x] kill by PID
- [x] Preemptive round-robin scheduler (120 Hz context switch via PIT)
- [x] Per-thread 8 KB stacks with full register save/restore
- [x] task_create_thread / task_yield / task_exit / task_block / task_unblock API
- [x] INT 0x80 yield interrupt (no PIT side effects)
- [x] Thread-safe malloc/free (irq_save/irq_restore guards)
- [x] Cooperative/preemptive hybrid: boot tasks share stack, spawned threads get own stacks
- [x] Deferred zombie cleanup (scheduler frees dead thread stacks safely)
- [x] Shell `spawn` command for testing (counter, hog)
- [x] Per-process page directories with CR3 switching on context switch
- [x] Ring 3 user mode: GDT user segments (0x1B/0x23), TSS, dual stacks
- [x] INT 0x80 syscall gate (10 syscalls: exit/yield/sleep/getpid/read/write/open/close/pipe/kill)
- [x] Per-task file descriptor table (16 FDs)
- [x] Kernel pipe buffers (4KB circular, blocking read/write, EOF/EPIPE)
- [x] Shell `|` pipe operator with grep/cat/wc right-side commands
- [x] `kill` command with signal flags (-9, -INT, -TERM, -KILL)

### Build System (Phase 12) — 100%
- [x] Cross-compiler toolchain (i686-elf-gcc)
- [x] Build chain: config.sh -> headers.sh -> build.sh -> iso.sh -> qemu.sh
- [x] Root Makefile: `make`, `make run`, `make clean`, `make clean-disk`
- [x] GRUB bootable ISO generation
- [x] QEMU + VirtualBox support
- [x] ~190 regression tests (string, stdlib, printf, FS, user, network, gfx, quota)

### Testing — 100%
- [x] ~190 test cases across all subsystems
- [x] String, stdlib, printf format, sscanf tests
- [x] File system: CRUD, permissions, quotas, indirect blocks, symlinks
- [x] Networking: IP/TCP/UDP parsing, endian conversion
- [x] User/group: permission checking, quota enforcement
- [x] Graphics: drawing primitives, font rendering

---

## What's Partially Done

| Feature | Status | What's Missing |
|---------|--------|----------------|
| Alpha transparency (G.6) | Optional | `gfx_blend_pixel()` exists but no full alpha channel API |
| GUI animations (I.7) | Partial | Basic crossfade works, no per-widget fade/slide animations |
| Firewall (P.4) | Basic | No stateful inspection, no reverse port forwarding |
| HTTP server | Basic | No CGI, no POST body handling, no HTTPS/TLS |
| vi editor | Functional | No regex search, no visual mode, no undo history |

---

## What's Missing

### Critical gaps for a "real" OS

| Category | Missing Feature | Impact |
|----------|----------------|--------|
| **Memory** | ~~Virtual memory / paging~~ | ✅ Done — identity-mapped 256MB, PMM bitmap allocator, page fault handler |
| **Memory** | Memory-mapped I/O abstraction | Direct physical addressing only |
| **Processes** | ~~Process isolation / ring 3~~ | ✅ Done — ring 3 user mode, per-process page tables |
| **Processes** | ~~Separate address spaces~~ | ✅ Done — per-process page directories with CR3 switching |
| **IPC** | ~~Pipes~~ | ✅ Done — kernel pipe buffers, FD table, shell `\|` operator |
| **IPC** | Shared memory / message passing | Tasks can't communicate safely |
| **IPC** | Signals (SIGINT, SIGTERM, etc.) | No async notification mechanism |
| **FS** | Journaling / crash recovery | Power loss = potential corruption |
| **FS** | File locking | No concurrent access protection |
| **FS** | Larger files (>69 KB) | Single-indirect limit, no double/triple indirect |
| **FS** | More inodes/blocks | Hard-capped at 64 inodes, 256 blocks |
| **Networking** | IPv6 | IPv4 only |
| **Networking** | TLS/SSL | No encrypted connections |
| **Networking** | HTTPS | HTTP only (no certificates, no crypto) |
| **Security** | Stack canaries / ASLR | No exploit mitigations |
| **Security** | ~~Syscall interface~~ | ✅ Done — INT 0x80 with 10 syscalls |
| **Hardware** | USB support | PS/2 keyboard/mouse only |
| **Hardware** | Audio / sound | No audio driver |
| **Hardware** | GPU acceleration | Software rendering only |
| **Hardware** | Real-time clock (RTC) | Time doesn't persist across reboots |
| **Platform** | 64-bit (x86_64) | i386 only |
| **Platform** | SMP / multi-core | Single-core only |
| **Platform** | EFI boot | BIOS/GRUB legacy only |

### Nice-to-have features not yet implemented

| Category | Feature |
|----------|---------|
| Shell | ~~Pipes (`\|`)~~, redirections (`>`, `<`, `>>`), backgrounding (`&`) |
| Shell | Scripting (variables, if/else, loops, functions) |
| Shell | Glob patterns (`*.c`, `?`) |
| FS | /proc or /sys virtual filesystem |
| FS | Mount/unmount, multiple partitions |
| FS | Hard links (currently only symlinks) |
| Networking | SSH client/server |
| Networking | FTP client |
| Networking | NTP time sync |
| Networking | WebSocket support |
| GUI | Clipboard (copy/paste between apps) |
| GUI | Drag-and-drop |
| GUI | Multiple workspaces/virtual desktops |
| GUI | Notification system |
| GUI | Scalable fonts (TTF/OTF) |
| GUI | Screen resolution switching at runtime |
| Apps | Calculator |
| Apps | Image viewer |
| Apps | Web browser (even minimal) |
| Apps | Package manager |
| libc | errno / perror |
| libc | time.h (time, ctime, strftime, mktime) |
| libc | ctype.h (isalpha, isdigit, etc.) |
| libc | math.h (sin, cos, sqrt — software float) |
| Docs | README with screenshots |
| Docs | Man pages for all commands |
| Docs | Developer guide / architecture doc |

---

## Known Limitations

1. **Partial process isolation** — Per-process page tables and ring 3 exist, but kernel memory is still identity-mapped and accessible. Full isolation requires unmapping kernel pages from user address space.
2. **Ring 3 with shared kernel mapping** — User threads run in ring 3 with INT 0x80 syscall gate (10 syscalls), but kernel memory is still mapped in user page tables.
3. **Identity-mapped kernel** — Paging enabled with identity mapping for first 256MB. Per-process user pages exist but kernel isn't in a higher-half mapping yet.
4. **69 KB max file size** — Single-indirect blocks limit. Would need double/triple indirect for larger files.
5. **64 inodes max** — Fixed filesystem capacity. Can't create more than 64 files/directories total.
6. **256 blocks max** — 128 KB total disk storage capacity (in the filesystem layer).
7. **No crash recovery** — A power loss during write can corrupt the filesystem. No journaling.
8. **Single-core only** — No SMP support, no spinlocks needed but also no parallelism.
9. **PS/2 only** — USB keyboards and mice are not supported.
10. **No real clock** — PIT counts ticks from boot, but there's no RTC driver for wall-clock time.
11. **8 TCP connections max** — Hard-coded limit on concurrent TCP sessions.
12. **No TLS** — All network traffic is plaintext.

---

## Future TODO List

### Priority 0 — MUST SHIP (Blockers — Cannot release without these)

> These are the fundamental gaps that make ImposOS non-competitive with any modern desktop OS (GNOME, KDE, etc). They must be resolved before any public release.

#### 0.1 — Process Model & Memory Protection
- [x] **Preemptive scheduling** — Timer-based context switch (120 Hz PIT-driven round-robin)
- [x] **Per-thread stacks** — 8 KB stacks, full register save/restore, cooperative/preemptive hybrid
- [x] **Paging / virtual memory** — Identity-map first, then page tables, page fault handler
- [x] **User mode (ring 3)** — TSS, syscall interface, separate user/kernel stacks
- [x] **Process isolation** — Per-process page tables, CR3 switching on context switch
- [x] **IPC: Pipes** — 4KB circular kernel pipe buffers, per-task FD table (16 FDs), blocking read/write, shell `|` operator with grep/cat/wc
- [ ] **IPC: Signals** — SIGINT (Ctrl+C kills), SIGTERM, SIGKILL, signal handlers
- [ ] **IPC: Shared memory / message passing** — Safe inter-process data exchange

> Preemptive multitasking, paging, ring 3 user mode, per-process page tables, and IPC pipes are done. Threads run with separate address spaces, INT 0x80 syscall gate (10 syscalls), and pipe-based IPC. Remaining: signals and shared memory.

#### 0.2 — Compositing & Rendering
- [ ] **Compositing window manager** — Each window renders to off-screen buffer, then composite with alpha/shadows/blur
- [ ] **Double/triple buffering with vsync** — Eliminate all tearing
- [ ] **TrueType font rendering** — Simplified FreeType-style TTF rasterizer (anti-aliased, hinted)
- [ ] **Resolution-independent drawing** — Vector primitives (lines, curves, fills) not just pixel pushing
- [ ] **HiDPI / DPI scaling** — Support for fractional scaling

> The single biggest visual quality jump. Bitmap fonts and raw framebuffer writes are immediately obvious to any user.

#### 0.3 — Clipboard & Core Desktop Integration
- [ ] **Clipboard system** — Copy/paste with MIME types between all apps
- [ ] **Virtual workspaces** — Multiple desktops with keyboard shortcut switching + animated transitions
- [ ] **Alt-Tab task switcher** — With live window previews
- [ ] **Window snapping** — Half/quarter screen snap-to-edge
- [ ] **Notification system** — Toast notifications with actions, do-not-disturb, history
- [ ] **Global keyboard shortcuts** — System-wide hotkey registry
- [ ] **Application launcher** — Search-based launcher (like GNOME Activities / KRunner)

> These are features every desktop user expects on day one.

#### 0.4 — Audio
- [ ] **Audio driver** — AC97 or Intel HDA (QEMU supports both)
- [ ] **Mixer / volume control** — Per-app volume, master volume, mute
- [ ] **System sounds** — Startup, notification, error beeps at minimum

> An OS without sound feels dead. Basic audio is a minimum shipping requirement.

#### 0.5 — Filesystem & Storage (Ship-blocking)
- [ ] **VFS layer** — Mountpoints, support multiple filesystem types
- [ ] **Expand capacity** — 256+ inodes, 4096+ blocks, larger disk
- [ ] **Double/triple indirect blocks** — Files up to several MB (not 69 KB)
- [ ] **Journaling** — Write-ahead log for crash recovery
- [ ] **MIME type detection** — File type → application association
- [ ] **Trash / recycle bin** — Soft delete instead of permanent rm

> A 69 KB file size limit and 64 files total makes the OS unusable for real work.

#### 0.6 — Widget Toolkit (Ship-blocking)
- [ ] **Layout engine** — Box/grid/flow layouts that auto-resize with window
- [ ] **CSS-like theming** — Stylesheets instead of hardcoded color values
- [ ] **Rich widgets** — Tree views, tab bars, combo boxes, date/color pickers, file dialogs, tooltips, popovers
- [ ] **Drag-and-drop** — Between and within applications
- [ ] **Keyboard navigation / focus chains** — Full keyboard-driven UI
- [ ] **Accessibility basics** — Focus indicators, keyboard-only mode

> Without a real layout engine, apps can't handle window resize properly. Users expect resize to work.

---

### Priority 1 — Polish & Ship (Weeks)

- [ ] **README.md** — Project overview, screenshots, build instructions, architecture diagram
- [ ] **Clean boot sequence** — ImposOS logo/splash, suppress debug output in production
- [ ] **Error messages** — User-friendly error messages for all commands
- [ ] **Reproducible demo script** — Document a walkthrough: boot -> login -> shell -> network -> GUI apps
- [ ] **Verify persistence** — Test full cycle: create files, reboot, verify they survived
- [ ] **Man pages** — Write help text for all 41+ commands
- [ ] **Screenshot gallery** — Capture desktop, apps, shell, login for documentation

### Priority 2 — Core OS Improvements (Months)

- [x] **Shell pipes** — `cmd1 | cmd2` with grep/cat/wc on right side
- [ ] **Shell redirections** — `cmd > file`, `cmd < file`, `cmd >> file`
- [ ] **RTC driver** — Read CMOS real-time clock, provide actual wall-clock time
- [ ] **ctype.h** — isalpha, isdigit, isspace, toupper, tolower
- [ ] **errno** — Global error code, perror(), strerror()

### Priority 3 — File System Enhancements (Months)

- [ ] **File locking** — Advisory locks for concurrent access
- [ ] **Hard links** — Multiple directory entries pointing to same inode
- [ ] **Mount/unmount** — Support multiple partitions or disk images
- [ ] **/proc filesystem** — Virtual FS exposing kernel state (tasks, memory, network)
- [ ] **Timestamps** — Created/modified/accessed times on inodes
- [ ] **File search indexing** — Background index for fast search (like Tracker/Baloo)

### Priority 4 — Networking Enhancements (Months)

- [ ] **NTP client** — Sync time from network time servers
- [ ] **HTTP improvements** — POST support, headers parsing, static file serving
- [ ] **SSH (stretch goal)** — Would need crypto primitives first
- [ ] **Stateful firewall** — Track connection state, allow established traffic
- [ ] **IPv6 (stretch goal)** — Dual-stack support
- [ ] **Larger TCP windows** — Increase from 4 KB buffers
- [ ] **DNS caching** — Cache resolved names to avoid repeated queries
- [ ] **HTTP client** — `wget`/`curl` equivalent command
- [ ] **TLS/SSL** — Encrypted connections (requires crypto primitives)

### Priority 5 — GUI & Desktop Enhancements (Months)

- [ ] **Window decorations** — Shadows, rounded corners, transparency
- [ ] **Window animations** — Open/close/minimize transitions
- [ ] **Widget animations** — Smooth fade/slide for panels and dialogs
- [ ] **Right-click context menus** — Already partial, expand everywhere
- [ ] **Multi-monitor support** — Extend, mirror, per-monitor DPI
- [ ] **Screen lock** — Lockscreen with PAM-style auth
- [ ] **Settings daemon** — Centralized config (like gsettings/KConfig)
- [ ] **Power management** — Suspend, hibernate, lid actions
- [ ] **System tray** — Battery, wifi, volume, bluetooth status indicators

### Priority 6 — Application Ecosystem (Months)

- [ ] **Terminal emulator** — VT100/xterm escape codes in a window
- [ ] **Text editor** — Syntax highlighting, undo/redo (beyond basic vi)
- [ ] **Calculator app** — Basic arithmetic
- [ ] **Image viewer** — BMP/simple format display
- [ ] **PDF viewer** — Minimal PDF renderer
- [ ] **Package manager** — Download and install packages over HTTP

### Priority 7 — Hardware & Platform (Long-term)

- [ ] **USB HID** — USB keyboard and mouse support (UHCI/OHCI/EHCI)
- [ ] **AHCI/SATA** — Modern disk controller support
- [ ] **VirtIO drivers** — Better QEMU performance (virtio-net, virtio-blk)
- [ ] **x86_64 port** — 64-bit long mode, larger address space
- [ ] **SMP** — Multi-core support, per-CPU data, spinlocks
- [ ] **EFI boot** — UEFI bootloader alongside legacy GRUB
- [ ] **Framebuffer modes** — Runtime resolution switching via VBE
- [ ] **GPU acceleration** — Basic OpenGL/Vulkan (Mesa-style, massive effort)

### Priority 8 — Stretch Goals / Experimental

- [ ] **Wayland-like display protocol** — Client-server display separation
- [ ] **Shell scripting** — Variables, if/else, while loops, functions
- [ ] **Glob patterns** — `*.c`, `file??.txt` in shell
- [ ] **ELF loader** — Load and execute separate ELF binaries
- [ ] **Dynamic linking** — Shared libraries (.so)
- [ ] **PE loader (Wine-lite)** — Run simple Windows console .exe
- [ ] **Web browser** — Minimal HTML renderer (text + links + CSS subset)
- [ ] **Crypto primitives** — SHA-256, AES (needed for TLS/SSH)
- [ ] **Ext2 read support** — Read real Linux partitions
- [ ] **Internationalization** — Unicode, RTL text, input methods, translations
- [ ] **D-Bus-like IPC** — System-wide message bus for app communication

---

## Architecture Summary

```
+------------------------------------------------------------------+
|                        USER SPACE (Ring 0*)                       |
|                                                                  |
|  +----------+  +-------+  +--------+  +---------+  +----------+ |
|  | File Mgr |  | Shell |  | vi     |  | Settings|  | Finder   | |
|  +----------+  +-------+  +--------+  +---------+  +----------+ |
|  +----------+  +---------+  +----------+                         |
|  | Task Mgr |  | Monitor |  | Login    |                         |
|  +----------+  +---------+  +----------+                         |
|                                                                  |
|  * Cooperative tasks in ring 0, spawned threads in ring 3         |
+------------------------------------------------------------------+
|                     WIDGET TOOLKIT (ui_widget.c)                  |
|  12 types: Label, Button, TextInput, List, Checkbox, Progress,   |
|  Tabs, Panel, Separator, Custom, Toggle, IconGrid, Card          |
+------------------------------------------------------------------+
|                    WINDOW MANAGER (wm.c)                         |
|  32 windows, Z-order, drag, resize, min/max/close               |
+------------------------------------------------------------------+
|                    DESKTOP (desktop.c)                            |
|  Splash -> Login -> Desktop (dock, clock, wallpaper)             |
+------------------------------------------------------------------+
|                    GRAPHICS (gfx.c)                               |
|  VBE 1920x1080x32, double buffer, primitives, alpha, font 8x16  |
+------------------------------------------------------------------+
|                        KERNEL SERVICES                           |
|  +------+ +------+ +-------+ +-------+ +--------+ +-------+ +----+|
|  |  FS  | | User | | Task  | | Sched | | Config | | Quota | |Env ||
|  +------+ +------+ +-------+ +-------+ +--------+ +-------+ +----+|
+------------------------------------------------------------------+
|                     NETWORKING STACK                              |
|  Socket API (16 sockets)                                         |
|  +---------+ +-----------+ +------+ +------+ +------+            |
|  | HTTP/1.0| | DHCP DORA | | DNS  | | TCP  | | UDP  |           |
|  +---------+ +-----------+ +------+ +------+ +------+            |
|  +------+ +------+ +------+ +----------+                         |
|  |  IP  | | ARP  | | ICMP | | Firewall |                         |
|  +------+ +------+ +------+ +----------+                         |
|  +----------+ +-----------+                                      |
|  | RTL8139  | | PCnet III | (auto-detected)                      |
|  +----------+ +-----------+                                      |
+------------------------------------------------------------------+
|                      HARDWARE LAYER                              |
|  +-----+ +-------+ +-------+ +------+ +------+ +------+         |
|  | GDT | | IDT   | | PIC   | | PIT  | | ATA  | | ACPI |        |
|  +-----+ +-------+ +-------+ +------+ +------+ +------+         |
|  +----------+ +-------+ +-----+                                  |
|  | Keyboard | | Mouse | | PCI |                                  |
|  +----------+ +-------+ +-----+                                  |
+------------------------------------------------------------------+
|                    LIBC (portable)                                |
|  string.h | stdio.h | stdlib.h | setjmp.h | stdarg.h             |
+------------------------------------------------------------------+
|                   BOOT (Multiboot / GRUB)                        |
+------------------------------------------------------------------+
```

### Shell Commands (41+)

| Category | Commands |
|----------|----------|
| **File ops** | cat, ls, cd, pwd, touch, mkdir, rm, chmod, chown, ln, readlink |
| **System** | help, man, echo, clear, history, exit, logout, shutdown, sync |
| **Editor** | vi |
| **Config** | export, env, setlayout, timedatectl, hostname |
| **Network** | ifconfig, ping, lspci, arp, nslookup, dhcp, httpd, connect, firewall |
| **Users** | whoami, su, id, useradd, userdel |
| **Debug** | test, gfxdemo, display, gfxbench, fps, top, kill, quota, spawn |

---

## File Map

```
impos/
├── Makefile                    # Build targets: make, make run, make clean
├── build.sh / iso.sh / qemu.sh # Build chain scripts
├── config.sh                   # Toolchain configuration
├── ROADMAP.md                  # Development roadmap (French)
├── progress.md                 # This file
│
├── kernel/
│   ├── kernel/kernel.c         # Main entry: init sequence + boot mode dispatch
│   ├── include/kernel/         # 38 header files
│   │   ├── io.h                # Port I/O (inb, outb)
│   │   ├── wm.h                # Window manager API
│   │   ├── ui_theme.h          # Theme colors/sizes
│   │   ├── ui_event.h          # Event ring buffer API
│   │   ├── ui_widget.h         # Widget toolkit API
│   │   ├── fs.h, net.h, ...    # Everything else
│   │   └── ...
│   └── arch/i386/
│       ├── boot.S              # Multiboot header, _start
│       ├── crti.S / crtn.S     # Constructor support
│       ├── isr_stubs.S         # Interrupt stubs (32 exceptions + 16 IRQs)
│       ├── linker.ld           # Kernel at 1 MiB
│       ├── idt.c               # GDT + IDT + PIC + PIT
│       ├── tty.c               # VGA text + graphical terminal
│       │
│       ├── drivers/
│       │   ├── ata.c           # ATA disk (LBA28, read/write/flush)
│       │   ├── mouse.c         # PS/2 mouse (3-byte packets)
│       │   ├── pci.c           # PCI bus enumeration
│       │   ├── acpi.c          # ACPI shutdown (S5 sleep state)
│       │   ├── rtl8139.c       # RTL8139 NIC (QEMU)
│       │   └── pcnet.c         # PCnet-FAST III NIC (VirtualBox)
│       │
│       ├── net/
│       │   ├── net.c           # Network abstraction + auto-detect
│       │   ├── arp.c           # ARP (16-entry cache)
│       │   ├── ip.c            # IPv4 + ICMP
│       │   ├── udp.c           # UDP (8 bindings)
│       │   ├── tcp.c           # TCP (8 connections, state machine)
│       │   ├── socket.c        # Socket API (16 sockets)
│       │   ├── dns.c           # DNS client
│       │   ├── dhcp.c          # DHCP DORA
│       │   ├── httpd.c         # HTTP/1.0 server
│       │   └── firewall.c      # Firewall (16 rules)
│       │
│       ├── gui/
│       │   ├── gfx.c           # Graphics engine (VBE, double buffer, primitives)
│       │   ├── font8x16.h      # Bitmap font (256 glyphs)
│       │   ├── desktop.c       # Desktop environment (splash/login/dock/clock)
│       │   ├── wm.c            # Window manager (32 windows, Z-order)
│       │   ├── ui_theme.c      # Theme system
│       │   ├── ui_event.c      # Event system
│       │   ├── ui_widget.c     # Widget toolkit (12 types)
│       │   ├── login.c         # Login GUI + setup wizard
│       │   ├── filemgr.c       # File manager app
│       │   ├── taskmgr.c       # Task manager app
│       │   ├── settings.c      # Settings app
│       │   ├── monitor.c       # Resource monitor app
│       │   └── finder.c        # Finder app (fuzzy search)
│       │
│       ├── sys/
│       │   ├── fs.c            # File system (inodes, blocks, dirs, symlinks)
│       │   ├── task.c          # Task tracking + preemptive thread lifecycle
│       │   ├── sched.c         # Round-robin preemptive scheduler
│       │   ├── user.c          # User management (/etc/passwd)
│       │   ├── group.c         # Group management (/etc/group)
│       │   ├── hash.c          # Password hashing
│       │   ├── env.c           # Environment variables
│       │   ├── config.c        # System configuration
│       │   ├── hostname.c      # Hostname management
│       │   ├── quota.c         # Per-user quotas
│       │   ├── pmm.c           # Physical memory manager (bitmap allocator)
│       │   ├── vmm.c           # Virtual memory manager (paging, per-process PDs)
│       │   ├── syscall.c       # INT 0x80 syscall handler (10 syscalls)
│       │   └── pipe.c          # IPC pipes (4KB circular buffers, FD table)
│       │
│       └── app/
│           ├── shell.c         # Shell (41+ commands, history, tab completion)
│           ├── vi.c            # vi modal text editor
│           └── test.c          # Regression tests (~190)
│
└── libc/
    ├── stdio/                  # printf, fprintf, fscanf, FILE API (8 files)
    ├── stdlib/                 # malloc/free, atoi, qsort, rand (12 files)
    ├── string/                 # String manipulation (17 files)
    └── setjmp/                 # setjmp.S (assembly)
```

---

## Stats

| Metric | Value |
|--------|-------|
| Total lines of code | ~28,200 |
| Kernel C files | 49 |
| Assembly files | 3 (.S) |
| libc files | 28 |
| Header files | 59 |
| Shell commands | 42+ |
| Regression tests | ~190 |
| GUI applications | 6 (filemgr, taskmgr, settings, monitor, finder, login) |
| Widget types | 12 |
| Max windows | 32 |
| Max tasks | 32 |
| Max files (inodes) | 64 |
| Max file size | ~69 KB |
| Network connections | 8 TCP + 8 UDP + 16 sockets |
| Firewall rules | 16 |
| Display | 1920x1080x32 (VBE) |
| Git commits | 68+ |
| Phases complete | 15/17 (core complete) |
