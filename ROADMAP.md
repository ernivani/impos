# ImposOS Roadmap: March - September 2026

## Goal

**A 70-80% presentable prototype** -- boots on real hardware, looks polished in
a demo, runs enough software to be impressive. Not production-ready, but enough
to launch a Kickstarter, post a demo video, and convince people this is real.

---

## Current State (End of February 2026)

**What exists (52K LOC of original code):**

| Subsystem | Status | LOC |
|-----------|--------|-----|
| Kernel core (task, sched, pmm, vmm, signals, pipes) | Working | ~1,500 |
| Filesystem (custom FS + tar initrd) | Working | ~1,300 |
| ELF loader (static Linux i386 binaries) | Working | ~380 |
| Linux syscall compat (~28 syscalls) | Working | ~900 |
| Win32 compat layer (kernel32, gdi32, user32, etc.) | Partial | ~10,700 |
| GUI (compositor, WM, desktop, menubar, dock) | Working | ~4,900 |
| Networking (TCP/IP, ARP, DHCP, DNS, HTTP, TLS) | Working | ~2,900 |
| Drivers (ATA, PCI, mouse, RTC, ACPI, NIC, VirtIO) | Working | ~2,500 |
| DOOM (native port via doomgeneric) | Working | Vendored |
| Graphics engine (gfx, paths, TTF, theming) | Working | ~2,500 |

**What's missing for a demo-ready prototype:**
- No process management syscalls (clone/execve/wait) -- can't run bash
- No socket syscalls -- Linux binaries can't use network
- No PTY -- no proper terminal emulation for Linux apps
- GUI apps are stubs (terminal, file manager, settings, task manager)
- No real hardware boot (only QEMU/VirtIO tested)
- No USB stack
- No audio
- No Android compat (long-term goal)
- i386 only (no AArch64 for phones)

---

## The Plan

### Assumptions
- 4-8 hours/day, every day
- AI-assisted development (Claude, etc.)
- Solo developer (possibly 1-2 contributors by summer)
- Target: desktop/laptop demo first, phone later
- "Presentable" = impressive 5-minute live demo, not daily-driver stable

---

## MARCH: Make Linux Binaries Actually Useful

**Theme: Process management + interactive shell**

The Linux compat layer can load and run individual static binaries, but they
can't spawn other processes. BusyBox runs but can't `sh -c "ls | grep foo"`.
This month makes the shell real.

### Week 1-2: Process Lifecycle Syscalls

| Syscall | Linux # | Work |
|---------|---------|------|
| `clone` (vfork semantics) | 120 | New task sharing parent address space until exec |
| `execve` | 11 | Load new ELF into existing task, reset state |
| `wait4` / `waitpid` | 114/7 | Block parent, return child exit status |
| `pipe` / `pipe2` | 42/331 | Already have pipes, wire to Linux fd numbers |
| `dup` / `dup2` / `dup3` | 41/63/330 | Copy fd table entries |
| `chdir` / `fchdir` | 12/133 | Set per-task cwd |
| `nanosleep` | 162 | Map to PIT sleep |
| `clock_gettime` | 265 | Return monotonic/realtime from RTC+PIT |
| `getppid` | 64 | Add parent_pid to task_info_t |

### Week 3: Signals

| Syscall | Linux # | Work |
|---------|---------|------|
| `rt_sigaction` | 174 | Register user signal handlers |
| `rt_sigprocmask` | 175 | Per-task signal mask |
| `rt_sigreturn` | 173 | Return from signal handler |
| `kill` | 37 | Already have sig_send_pid, wire it |
| `tkill` / `tgkill` | 238/270 | Thread-directed signals |

### Week 4: Integration + Testing

- BusyBox ash running commands: `ls -la | grep foo | wc -l`
- Static bash binary launches and runs scripts
- Shell pipelines, redirections (`>`, `>>`, `<`, `2>&1`)
- Job control basics (Ctrl+C sends SIGINT, Ctrl+Z sends SIGTSTP)
- `busybox vi`, `busybox less`, `busybox top` all interactive

### March Deliverable
> Boot ImposOS, get a real shell, run `ls | grep .c | wc -l` and see the
> correct count. Run `busybox vi` and edit a file. Ctrl+C kills a process.

---

## APRIL: GUI Apps + Desktop Polish

**Theme: The desktop goes from "tech demo" to "usable environment"**

### Week 1-2: App Model + Terminal

- Implement `app_class_t` lifecycle (on_create, on_event, on_paint, on_destroy)
- App registry + `app_launch()` / `app_destroy()`
- Desktop loop routes events to focused app
- **Terminal app**: VT100 emulator in a window
  - 80x24 character grid (configurable)
  - ANSI color (16 colors minimum, 256 nice-to-have)
  - Scrollback buffer (1000 lines)
  - Connects to BusyBox ash or static bash
  - Copy/paste (Ctrl+Shift+C/V)
- Wire the terminal to a **PTY** (pseudo-terminal):
  - `openpty` / `posix_openpt` syscalls
  - Line discipline: raw mode + cooked mode
  - Terminal ioctls: TCGETS, TCSETS, TIOCGWINSZ

### Week 3: File Manager + Settings

- **File Manager app**:
  - List view with icons (file, folder, executable)
  - Breadcrumb navigation bar
  - Double-click to open folders, single-click to select
  - Right-click context menu (copy, paste, delete, rename)
  - Shows file size, modified date
  - Navigate initrd + writable FS
- **Settings app**:
  - About panel (OS name, version, uptime, memory usage)
  - Display: resolution info, wallpaper selector
  - Theme: switch between Catppuccin flavors (Mocha, Latte, Macchiato)
  - Keyboard: layout display (informational for now)

### Week 4: Desktop Shell Polish

- **Alt+Tab** window switcher with thumbnails
- **Dock**: running indicators (dots), hover tooltips, click-to-focus
- **Menubar**: working dropdown menus for focused app (File, Edit, View)
- **Right-click desktop** -> context menu (New Folder, Change Wallpaper, About)
- **Notification toasts**: slide-in from top-right, auto-dismiss
- Window snap: drag to edge -> half-screen
- Keyboard shortcuts: Ctrl+Q quit, Ctrl+W close window, Ctrl+N new window

### April Deliverable
> Open Terminal from the dock, type commands, see colorized output. Open File
> Manager, browse folders. Change theme in Settings. Alt+Tab between windows.
> Looks like a real desktop OS in screenshots.

---

## MAY: Networking for Linux Binaries + Web Browser

**Theme: Connect to the internet, browse websites**

### Week 1-2: Socket Syscalls

The TCP/IP stack exists (2,900 LOC). It just isn't wired to Linux syscalls.

| Syscall | Linux # | Work |
|---------|---------|------|
| `socketcall` | 102 | Multiplex: socket/bind/connect/send/recv/etc. |
| `socket` | via 102 | Map to ImposOS socket_create |
| `connect` | via 102 | Map to tcp_connect |
| `send` / `sendto` | via 102 | Map to tcp_send / udp_send |
| `recv` / `recvfrom` | via 102 | Map to tcp_recv / udp_recv |
| `bind` / `listen` / `accept` | via 102 | Server-side socket ops |
| `select` | 142 | fd-set polling over sockets + pipes + files |
| `poll` | 168 | Same, struct pollfd interface |
| `getsockopt` / `setsockopt` | via 102 | Basic socket options |
| `getpeername` / `getsockname` | via 102 | Address queries |

### Week 2-3: Test With Real Binaries

- Static `curl` fetches a web page
- Static `wget` downloads a file
- `busybox wget http://example.com` works
- DNS resolution works from Linux binaries (`getaddrinfo` -> ImposOS DNS)

### Week 3-4: Web Browser (NetSurf Framebuffer)

- Cross-compile NetSurf with framebuffer frontend for ImposOS
- ImposOS backend (~200 lines):
  - Create a WM window for the browser
  - Point NetSurf framebuffer at window pixel buffer
  - Route mouse/keyboard from WM to NetSurf input
- Dependencies: libcss, hubbub (bundled), libcurl, libpng, freetype2
- **Fallback**: Static `links -g` (graphical Links browser) if NetSurf is too heavy

### May Deliverable
> Open a terminal, run `curl http://example.com`, see HTML. Launch the browser
> app from the dock, type a URL, see a rendered web page with CSS and images.
> This is the "holy shit" demo moment.

---

## JUNE: Hardware + Stability

**Theme: Boot on real hardware, stop crashing**

### Week 1: USB Stack (XHCI/EHCI)

- XHCI host controller driver (modern USB 3.0)
- EHCI fallback (USB 2.0, wider hardware support)
- USB HID: keyboard and mouse via USB (not just PS/2)
- USB mass storage: mount USB drives (read-only is fine)
- This unlocks: real keyboards, real mice, USB boot drives

### Week 2: Storage

- AHCI driver (SATA) -- most real hardware uses AHCI, not legacy ATA
- Persistent read-write filesystem (ext2 is simplest, well-documented)
- Or: expand custom FS to support persistence via AHCI
- Boot from real disk (not just GRUB module)

### Week 3: Real Hardware Bringup

- Pick a target machine (cheap ThinkPad, or a mini-PC like Intel NUC)
- Fix the 100 things that break when you leave QEMU:
  - ACPI table parsing for real hardware
  - PCI enumeration for real devices
  - Framebuffer: VBE mode setting on real GPU (not BGA/VirtIO)
  - IRQ routing on real IOAPIC
  - Memory map from real E820
- Serial console for debugging (when the screen shows nothing)

### Week 4: Stability Pass

- Fix crash-on-close bugs in WM (use-after-free, dangling surfaces)
- Handle out-of-memory gracefully (don't panic, show "low memory" toast)
- Watchdog: detect hung tasks, offer to kill them
- Clean shutdown: ACPI poweroff sequence
- Stress test: open 10 terminal windows, run commands in all of them

### June Deliverable
> ImposOS boots on a real laptop from a USB stick. Keyboard and mouse work.
> Desktop appears. Terminal works. Browser works. It doesn't crash for
> 10 minutes of demo usage. This is the Kickstarter video hardware.

---

## JULY: Content Creation Month (Kickstarter Prep)

**Theme: Build the demo, tell the story, launch the campaign**

### Week 1: Demo Apps + Eye Candy

- **Task Manager app**: process list, CPU/memory bars, kill button
- **System Monitor app**: live CPU, memory, network sparkline graphs
- **Text Editor app**: basic editor with syntax highlighting (wrap vi or custom)
- **Calculator app**: simple calculator (looks good in screenshots)
- **Image Viewer**: display BMP/PNG images from filesystem
- Window **animations**: open (scale up + fade), close (scale down + fade)
- Dock **magnification** on hover (the macOS effect)
- **Login screen** polish: user avatar, password field, fade-in transition

### Week 2: DOOM as a Windowed App

- Wrap DOOM in app_class_t (currently runs fullscreen)
- Runs in a resizable window alongside other apps
- Pause when unfocused, resume when focused
- This is the demo crowd-pleaser -- "I built an OS and DOOM runs in a window"

### Week 3: Demo Video Production

- Script a 3-minute demo:
  1. Cold boot on real hardware (10s)
  2. Login screen -> desktop appears (10s)
  3. Open terminal, run some commands, show Linux compat (30s)
  4. Open file manager, browse files (15s)
  5. Open browser, load a web page (20s)
  6. Open DOOM in a window, play for 15 seconds (15s)
  7. Alt+Tab between everything, show it all running (10s)
  8. Show task manager with all processes (10s)
  9. "Built from scratch. 60,000 lines of code. One developer." (10s)
- Record on real hardware (not QEMU -- people can tell)
- Clean audio, simple background music, text overlays

### Week 4: Kickstarter Campaign

- Write the campaign page:
  - **Story**: "I'm building an independent OS from scratch"
  - **What exists**: demo video, screenshots, git history as proof
  - **What the money is for**: hardware, contributor bounties, NDA docs
  - **Tiers**: $10 supporter, $25 beta tester, $50 name in bootloader,
    $100 early dev board, $500 private Discord + roadmap input
- **Goal**: $25,000-50,000 (modest, hittable)
- Launch on a Tuesday (best day for Kickstarter launches)
- Cross-post: Reddit (r/osdev, r/linux, r/programming), HN, Twitter/X, YouTube

### July Deliverable
> Kickstarter is live with a polished demo video. The video shows a real OS
> booting on real hardware with a desktop, terminal, browser, file manager,
> and DOOM. Campaign is funded (or on track to fund).

---

## AUGUST: Linux Compat Depth + Developer Story

**Theme: Run more software, attract developers**

### Week 1-2: Expand Linux Syscall Coverage

Get the long tail of syscalls that popular binaries need:

| Target | New syscalls needed |
|--------|-------------------|
| Python 3 (static) | `mprotect`, `futex`, `getrlimit`, `sysinfo` |
| Git (static) | `rename`, `unlink`, `mkdir`, `rmdir`, `chmod`, `utimes` |
| GCC (static) | `vfork` (real), `access`, `readlink`, `symlink` |
| tmux | Unix domain sockets, `epoll_create/ctl/wait` |
| htop | `/proc` filesystem (virtual, read-only) |

- Implement `/proc/self/status`, `/proc/meminfo`, `/proc/cpuinfo`, `/proc/stat`
- Implement `/dev/null`, `/dev/zero`, `/dev/urandom` (if not already)
- Total new syscalls: ~20-25
- Target: ~50-55 Linux syscalls total (enough for most CLI tools)

### Week 3: Developer SDK

- **Cross-compilation guide**: "How to build an ImposOS native app"
  - i686-elf-gcc toolchain setup on Linux/macOS
  - app_class_t template with Makefile
  - Graphics API reference (gfx_*, compositor, WM)
- **ImposOS simulator**: a thin wrapper that runs the GUI in an SDL window
  on the host machine (like iOS Simulator)
  - Compile GUI code against SDL2 instead of bare-metal framebuffer
  - Same app_class_t API, same pixel buffer
  - Faster iteration: no QEMU boot cycle
- **Example apps**: 3 documented example apps (hello-window, drawing-app, todo-list)

### Week 4: SMP (Multi-Core)

- AP (Application Processor) startup via APIC INIT+SIPI sequence
- Per-CPU run queues in the scheduler
- Spinlocks / mutexes for shared kernel data structures
- This matters for real hardware (every modern CPU has 4+ cores)
- Even if apps don't use threads, the kernel benefits (IRQ handling on AP,
  apps on BSP)

### August Deliverable
> Python REPL works on ImposOS. `git status` works. htop shows real system
> stats from /proc. A developer can download the SDK, write a native app,
> and test it in the simulator without booting a VM. SMP makes the kernel
> noticeably faster on real hardware.

---

## SEPTEMBER: Polish + Demo Day

**Theme: Everything works together, nothing crashes**

### Week 1: Integration Testing

- Automated test suite (run in QEMU with serial output):
  - Kernel unit tests: PMM, VMM, scheduler, FS, pipe, signal
  - Linux compat tests: run 20 static binaries, check output
  - GUI smoke test: launch each app, verify no crash after 30 seconds
- Fix every crash found in testing
- Memory leak audit (track kmalloc/kfree balance)

### Week 2: Performance + UX Polish

- **Framerate**: target 30fps in compositor (currently uncapped spin loop)
  - Add frame timing, skip recomposite if no damage
  - Profile hotspots (alpha blending, font rendering)
- **Boot time**: minimize kernel init (lazy driver probe, parallel init)
- **Font rendering**: switch from 8x16 bitmap to TTF for UI text
  (TTF rasterizer exists, just not wired to WM/apps)
- **Sound**: Intel HDA driver for basic audio
  - System sounds: startup chime, notification ding
  - DOOM audio (already has sound code, just needs a real audio driver)
- **Wallpapers**: bundle 3-4 high-quality wallpapers in initrd

### Week 3: Final Demo Build

- Create a "release image":
  - Bootable ISO (GRUB + kernel + initrd with all apps)
  - Bootable USB image (dd to USB stick)
  - QEMU launch script for easy testing
- Update Kickstarter with progress video (if campaign is still running)
- Blog post: "7 months of building an OS from scratch"

### Week 4: Community + Next Steps

- Open-source cleanup: license headers, CONTRIBUTING.md, issue templates
- Tag v0.1.0 release on GitHub
- Post demo to: HN ("Show HN"), r/osdev, r/programming, OSDev Wiki
- Write up what's next: AArch64 port, Android compat, phone hardware
- Engage with early contributors from Kickstarter / GitHub

### September Deliverable
> **The demo**: ImposOS v0.1.0 boots on real hardware in under 10 seconds.
> Desktop with menubar, dock, and wallpaper. 7 native apps (Terminal, File
> Manager, Settings, Task Manager, Text Editor, System Monitor, DOOM).
> Web browser renders real pages. 50+ Linux CLI tools via BusyBox + bash.
> Python, curl, git work. Sound plays. Looks polished in screenshots and
> video. Bootable ISO anyone can try in a VM.

---

## Summary: What 70-80% Looks Like

### What works in the demo (the 70-80%)

- Clean boot on real hardware (1 specific machine) or any VM
- macOS-style desktop: menubar, dock, composited windows, animations
- 7+ native GUI apps, all functional
- Terminal with real shell (bash) and 300+ Unix commands (BusyBox)
- Web browser that renders real websites
- DOOM in a windowed app
- Networking: curl, wget, browser all fetch real URLs
- Python REPL, basic git operations
- Sound (startup chime, DOOM audio)
- Multi-core support
- USB keyboard/mouse

### What doesn't work yet (the 20-30%)

- Only runs on 1-2 tested hardware configs (not universal)
- No phone hardware / AArch64
- No Android app compatibility
- No GPU acceleration (all software rendering)
- No Bluetooth, Wi-Fi (Ethernet only)
- No power management / battery
- No automatic updates
- No app store
- No multi-user security
- Some Linux binaries crash on uncommon syscalls
- No dynamic linking (static binaries only)
- Edge cases crash (they always do)

### Lines of code estimate

| Area | Current | Added | Total |
|------|---------|-------|-------|
| Kernel core + syscalls | ~3,000 | +4,000 | ~7,000 |
| Linux compat | ~900 | +3,000 | ~4,000 |
| GUI + apps | ~5,000 | +8,000 | ~13,000 |
| Drivers (USB, AHCI, HDA, SMP) | ~2,500 | +5,000 | ~7,500 |
| Networking (socket syscalls) | ~2,900 | +1,000 | ~3,900 |
| Build system + tools | ~500 | +1,000 | ~1,500 |
| **Total original code** | **~53,000** | **+22,000** | **~75,000** |

Plus vendored code (DOOM, potentially NetSurf/freetype2).

---

## Monthly Milestones at a Glance

| Month | Theme | Key Deliverable |
|-------|-------|----------------|
| **March** | Process Management | bash runs, pipelines work, vi edits files |
| **April** | GUI Apps | Terminal, File Manager, Settings -- looks like a real desktop |
| **May** | Networking + Browser | curl fetches URLs, browser renders web pages |
| **June** | Real Hardware | Boots on a real laptop, USB works, doesn't crash |
| **July** | Kickstarter | Demo video, campaign launches, DOOM in a window |
| **August** | Developer Story | Python/git work, SDK published, SMP |
| **September** | Polish + Release | v0.1.0 ISO, blog post, community launch |

---

## Decision Points

These are forks in the road where you'll need to commit:

1. **March**: Do you keep i386 or start an x86_64 port?
   - Recommendation: **Stay i386 through September.** Switching architectures
     mid-sprint kills momentum. x86_64 is a post-Kickstarter project.

2. **May**: NetSurf or graphical Links for the browser?
   - Try NetSurf first (2 weeks). If it's too heavy, fall back to Links.

3. **June**: Which hardware target?
   - Pick ONE specific machine. Buy two of them (one to break).
   - Recommendation: ThinkPad X220/X230 (cheap, well-documented, Intel GPU).

4. **July**: Kickstarter goal amount?
   - $25K is modest and hittable. $50K is ambitious but credible with a good video.
   - Don't go above $50K. Under-promise, over-deliver.

5. **August**: Open-source now or after Kickstarter?
   - It's already on GitHub. Lean into it. Open-source builds trust for the campaign.

---

## Risk Register

| Risk | Impact | Mitigation |
|------|--------|------------|
| Real hardware won't boot | Blocks June + July | Start hardware testing in May (parallel to browser work) |
| NetSurf too complex to port | No browser demo | Fall back to graphical Links (much simpler) |
| USB stack takes too long | No real hardware input | PS/2 keyboards still exist; buy a USB-to-PS/2 adapter |
| Kickstarter fails to fund | No money | The OS still exists; pivot to GitHub Sponsors + NLnet grants |
| Burnout from daily 4-8hr schedule | Everything stops | Build in rest days. One day off per week minimum. |
| Key syscall is brutally hard | Linux compat stalls | Stub it, log "UNIMPL: syscall X", move on. 70% is the target. |
| SMP introduces heisenbugs | Kernel instability | Keep SMP optional. Ship with UP (uniprocessor) as fallback. |
