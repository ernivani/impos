# ImposOS Modernization Upgrade Plan

> Linear upgrade path to bring ImposOS from ~1997 era to ~2015 across all subsystems.
> Each phase builds on the previous one. Follow in order.

---

## Phases 1–11 — COMPLETED

| Phase | Summary | Status |
|-------|---------|--------|
| 1 — Filesystem Overhaul | FS v4, 256MB/4096 inodes, double-indirect, journaling, VFS, procfs/devfs/tmpfs | Done |
| 2 — Process Model & Scheduling | 256 fds, clone/threads, pthreads, signals, process groups, waitpid | Done |
| 3 — Memory Management | Per-process address spaces, demand paging, mmap, shared memory | Done |
| 4 — Expanded Syscall Interface | 76 Linux syscalls, poll, ioctl, futex, readv/writev | Done |
| 5 — Dynamic Linking | ELF dynamic loader, musl ldso, file-backed mmap, PT_INTERP | Done |
| 6 — Networking Modernization | Non-blocking sockets, TCP improvements, full BSD socket API | Done |
| 7 — Win32 Compatibility Expansion | 11 DLL shims, SEH, PE loader, threading, memory, GUI APIs | Done |
| 8 — Desktop Environment & IPC | Window manager, widget toolkit, notifications, system tray, desktop apps | Done |
| 9 — Audio Subsystem | AC'97 driver, audio mixer, DOOM windowed mode | Done |
| 10 — Filesystem Hardlinks & VM | Hardlinks, VM improvements, HTTP client, USB enumeration | Done |
| 11 — HTTPS & TLS Expansion | TLS 1.2 with 4 cipher suites, ECDHE/GCM, wget HTTPS, redirect following | Done |

**Current state after Phase 11:** ~2008 era across most subsystems. ~148K LOC, 1146 tests, 4 TLS cipher suites, GPU-accelerated desktop, Win32+Linux binary compat.

---

## Phase 12 — I/O Multiplexing & Shell Scripting

**Current state:** poll() exists in Linux syscall compat, shell has 64 commands but no scripting.
**Target state:** ~2010 era shell with proper I/O multiplexing.

The shell is the primary user interface for a lot of OS functionality. Without scripting, every task is manual. Without proper select/poll on native fds, event-driven apps can't work.

### Step 12.1 — Native poll()/select() for All FD Types

- Ensure poll/select works natively (not just Linux compat) across pipes, sockets, device fds, and regular files
- Add `O_NONBLOCK` support on pipes and device fds (not just sockets)
- Implement `POLLIN`, `POLLOUT`, `POLLHUP`, `POLLERR` properly for each fd type
- This is the foundation for any event loop (GUI, server, terminal multiplexer)

### Step 12.2 — Shell Variable Expansion

- Environment variable expansion: `$HOME`, `$PATH`, `$?` (last exit code)
- Variable assignment: `FOO=bar`
- Double-quote interpolation: `"hello $USER"` vs single-quote literal: `'hello $USER'`
- Special variables: `$0`, `$1`..`$9`, `$#`, `$@`, `$$`

### Step 12.3 — Shell Redirection & Globbing

- Output redirection: `>`, `>>` (append), `2>` (stderr), `2>&1`
- Input redirection: `<`
- Here-documents: `<<EOF`
- Glob expansion: `*`, `?`, `[abc]` patterns via a simple `glob()` implementation
- Depends on dup/dup2 (already in Linux syscall layer — expose natively)

### Step 12.4 — Shell Control Flow

- `if`/`then`/`elif`/`else`/`fi`
- `for` loops: `for x in *.c; do echo $x; done`
- `while`/`until` loops
- `case`/`esac` pattern matching
- Exit code testing: `&&`, `||`
- Subshells: `$(command)` or backtick substitution

### Step 12.5 — Job Control

- Background execution: `command &`
- `fg`, `bg` builtins
- `Ctrl+Z` sends `SIGTSTP` to foreground process group
- `jobs` builtin to list background jobs
- Depends on process groups and sessions (already implemented)

### Step 12.6 — Core Userspace Utilities

- `cp`, `mv` (currently missing — basic file operations)
- `grep` (basic pattern matching, no full regex needed)
- `wc` (line/word/byte count)
- `head`, `tail` (first/last N lines)
- `sort`, `uniq`
- `find` (recursive directory search with name matching)
- `tee` (write to stdout and file simultaneously)
- `xargs` (build commands from stdin)

---

## Phase 13 — Image Decoding & Desktop Polish

**Current state:** TrueType fonts, vector path graphics, GNOME-dark theme, but no image format support.
**Target state:** ~2010 era desktop that can display images and feels cohesive.

Every desktop OS from 2000 onward can display PNG and JPEG. This is a major usability gap.

### Step 13.1 — PNG Decoder

- Implement a minimal PNG decoder: IHDR, IDAT (deflate), IEND chunks
- Support 8-bit RGBA and RGB, grayscale
- Inflate (zlib decompress) — you can port a minimal `tinfl` or write one from RFC 1951
- Filter reconstruction (None, Sub, Up, Average, Paeth)
- Output to 32-bit BGRA framebuffer format for direct compositor use

### Step 13.2 — JPEG Decoder

- Implement baseline JPEG decoding (DCT-based, Huffman, YCbCr→RGB)
- A minimal stb_image-style decoder is ~1500 LOC in C
- Support 4:2:0 and 4:4:4 chroma subsampling
- No need for progressive JPEG initially

### Step 13.3 — BMP/ICO Support

- BMP is trivial (raw pixel data with a header) — useful for icons and cursors
- ICO format for window icons and taskbar (wraps BMP or PNG)
- Use for custom app icons in the desktop, file manager thumbnails

### Step 13.4 — Image Viewer Application

- Desktop app: open PNG/JPEG/BMP files from the file manager
- Zoom, pan, fit-to-window
- Integrate with file manager: double-click an image opens the viewer
- Thumbnail generation in file manager directory listings

### Step 13.5 — Wallpaper & Theming Improvements

- Load a PNG/JPEG wallpaper at boot instead of a solid color
- Wallpaper selection in Settings app
- Icon themes: PNG-based icons for files, folders, apps in the file manager and dock
- Cursor themes (custom cursor images from PNG/BMP)

### Step 13.6 — Clipboard

- Implement a global clipboard buffer (kernel-managed or via shared memory)
- Copy/paste plain text between terminal, notes, and other text widgets
- `Ctrl+C` / `Ctrl+V` keybindings in text input widgets
- Later: support image clipboard data

---

## Phase 14 — SMP (Symmetric Multiprocessing)

**Current state:** Single CPU, PIC-based interrupts, round-robin scheduler.
**Target state:** ~2005 era (Linux 2.6 SMP, dual/quad core support).

By 2010 every desktop machine has multiple cores. SMP is the single biggest architectural leap remaining. This is the hardest phase.

### Step 14.1 — APIC & IOAPIC

- Replace legacy 8259 PIC with Local APIC + IOAPIC
- Parse ACPI MADT table to discover CPU cores and IOAPIC
- Program IOAPIC routing entries for IRQ delivery
- Local APIC timer replaces PIT for per-CPU scheduling ticks
- Spurious interrupt handler

### Step 14.2 — AP Startup (Application Processor Bootstrap)

- Send INIT + SIPI (Startup IPI) sequence to bring up secondary cores
- AP trampoline code: real mode → protected mode → paging → kernel entry
- Each AP gets its own GDT, IDT, TSS, kernel stack
- Per-CPU data structure (current task, runqueue, APIC ID)

### Step 14.3 — SMP-Safe Kernel

- Add spinlocks (`spin_lock`, `spin_unlock` with `lock` prefix on x86)
- Identify and lock all shared data: scheduler runqueue, PMM bitmap, fd tables, VFS caches, network buffers
- Replace interrupt-disable critical sections with proper spinlocks + interrupt disable
- Per-CPU idle task

### Step 14.4 — SMP Scheduler

- Per-CPU runqueues (each core has its own task list)
- Load balancing: migrate tasks from overloaded to idle CPUs
- CPU affinity: tasks can prefer specific cores
- IPI (Inter-Processor Interrupt) for cross-CPU wakeups and TLB shootdowns

### Step 14.5 — TLB Management

- TLB shootdown via IPI when page tables change (munmap, CoW, process exit)
- Per-CPU `cr3` tracking
- Lazy TLB invalidation for kernel threads (no user address space)

---

## Phase 15 — Networking Maturity

**Current state:** TCP/UDP, TLS 1.2 with 4 cipher suites, HTTP GET with redirects, 8 max connections.
**Target state:** ~2012 era networking.

### Step 15.1 — TCP Window Scaling & Performance

- TCP window scaling (RFC 1323) — current 4KB buffer limits throughput
- Increase TCP buffer to 64KB (or configurable per-socket)
- TCP SACK (Selective Acknowledgment) for better loss recovery
- TCP congestion control: slow start + congestion avoidance (NewReno at minimum)
- Raise max TCP connections from 8 to at least 64

### Step 15.2 — Loopback Interface

- `lo` interface at `127.0.0.1`
- Deliver packets directly to the receive path without going through a NIC driver
- Required for local services, testing, and Unix convention

### Step 15.3 — HTTP POST & Chunked Transfer

- HTTP client: POST method with `Content-Length` body
- Chunked transfer-encoding decoding (receiving)
- `Content-Type` handling for form data (`application/x-www-form-urlencoded`, `multipart/form-data`)
- Needed for any meaningful web interaction beyond GETs

### Step 15.4 — TLS SNI (Server Name Indication)

- Send SNI extension in TLS ClientHello
- Many HTTPS servers (Cloudflare, AWS, shared hosting) require SNI by ~2013
- Without this, `wget https://` fails on most modern sites

### Step 15.5 — UDP sendto/recvfrom & DNS Improvements

- Proper `sendto()`/`recvfrom()` for UDP sockets (not just internal DNS use)
- DNS response caching (TTL-based)
- DNS TCP fallback for large responses
- Multiple DNS server support

### Step 15.6 — Netcat / Telnet Utility

- `nc` command: arbitrary TCP/UDP connections from the shell
- Useful as a debug tool and for simple data transfer
- Listen mode: `nc -l 8080` for ad-hoc servers

---

## Phase 16 — USB Device Classes

**Current state:** UHCI host controller with enumeration only. No device class drivers.
**Target state:** ~2008 era (functional USB keyboard, mouse, storage).

### Step 16.1 — USB Transfer Infrastructure

- Implement Control, Bulk, and Interrupt transfer types on UHCI
- TD (Transfer Descriptor) and QH (Queue Head) management
- Proper error handling and retry logic
- USB request block (URB) abstraction

### Step 16.2 — USB HID (Human Interface Devices)

- USB keyboard driver: HID report parsing, keycode translation
- USB mouse driver: relative/absolute reports, button state
- Hot-plug detection: port status change → device enumeration → driver binding
- Fall back gracefully when PS/2 devices are also present

### Step 16.3 — USB Mass Storage (Bulk-Only Transport)

- BOT (Bulk-Only Transport) protocol: CBW/CSW/data phases
- SCSI command set: INQUIRY, READ CAPACITY, READ(10), WRITE(10)
- Block device layer: expose as `/dev/sda`, `/dev/sdb`
- Mount via VFS (requires a readable FS — FAT32 in Step 16.4)

### Step 16.4 — FAT32 Read Support

- Read-only FAT32 filesystem driver (sufficient for USB sticks and SD cards)
- Parse BPB (BIOS Parameter Block), FAT table, directory entries
- Long File Name (LFN) support (VFAT)
- Mount via VFS: `mount /dev/sda1 /mnt/usb`

### Step 16.5 — USB Hub Support

- Hub descriptor parsing, port power management
- Cascaded hubs (hub behind hub)
- Port status change handling for nested hot-plug

---

## Phase 17 — Audio Expansion

**Current state:** AC'97 driver, audio mixer, but no userspace audio API or file playback.
**Target state:** ~2008 era (OSS/ALSA-level audio with app integration).

### Step 17.1 — Audio Device Interface

- Create `/dev/dsp` (OSS-compatible) or `/dev/snd/pcmC0D0p` device node
- `open`, `write`, `ioctl` interface for PCM playback
- ioctls: set sample rate, format (S16LE, U8), channels (mono/stereo)
- Basic ring buffer with blocking writes when buffer is full

### Step 17.2 — WAV File Playback

- WAV file parser (RIFF header, fmt/data chunks)
- `play` shell command: `play /sounds/startup.wav`
- Sample rate conversion (nearest-neighbor or linear interpolation) if WAV rate ≠ 48kHz
- 8-bit to 16-bit conversion

### Step 17.3 — Multi-Stream Mixing

- Expand the audio mixer to support N simultaneous streams (not just master volume)
- Per-stream sample rate, volume, and format conversion
- Mix by summing with clipping protection
- At least 4 concurrent streams (system sounds + music + app + game)

### Step 17.4 — System Sounds

- Startup sound, shutdown sound
- Notification chime, error beep (replace PC speaker)
- Window open/close sounds (optional, toggleable in Settings)
- Sounds stored as WAV in initrd or on disk

### Step 17.5 — DOOM Audio

- Hook DOOM's audio subsystem to the audio mixer
- SFX: PCM playback through `/dev/dsp` or direct mixer API
- Music: OPL2/OPL3 FM synthesis or MUS→PCM conversion
- Simultaneous game audio + system sounds via mixing

---

## Phase 18 — Security Hardening

**Current state:** Unix permissions (rwx), user/group, ring 0/ring 3 separation. No ASLR, NX, or stack protection.
**Target state:** ~2010 era (basic exploit mitigations).

### Step 18.1 — NX Bit (No-Execute)

- Enable NX/XD bit via PAE or PSE (requires EFER.NXE on x86)
- Mark stack and heap pages as non-executable
- Mark code pages as non-writable
- This stops the most basic code injection attacks

### Step 18.2 — Stack Canaries

- Compile with `-fstack-protector` or implement manual canaries
- Random canary value from CSPRNG, checked on function return
- `__stack_chk_fail` handler: kill the process with SIGABRT

### Step 18.3 — ASLR (Address Space Layout Randomization)

- Randomize user stack base, mmap base, heap start, and ELF load address
- CSPRNG-seeded offsets (you already have `/dev/urandom`)
- Per-exec randomization (new layout on each `execve`)
- Even basic ASLR (8-12 bits of entropy) raises the bar significantly

### Step 18.4 — Password Hashing

- Implement bcrypt or SHA-512 crypt for `/etc/shadow`
- Replace plaintext or simple hash password storage
- Salt generation from CSPRNG
- `passwd` command to change passwords

### Step 18.5 — Kernel Stack Isolation

- Separate kernel stacks completely from user-accessible memory
- Guard pages above/below kernel stacks (unmapped pages that trap on overflow)
- Prevents kernel stack buffer overflows from corrupting adjacent memory

---

## Phase 19 — Expanded Binary Compatibility

**Current state:** 76 Linux syscalls, 11 Win32 DLLs. Can run static and dynamically-linked ELF32.
**Target state:** ~2010 era (run real-world small Linux utilities natively).

### Step 19.1 — Missing Critical Syscalls

- `select()` / `pselect6` (many programs prefer select over poll)
- `epoll_create`, `epoll_ctl`, `epoll_wait` (modern event-driven programs)
- `eventfd`, `signalfd`, `timerfd` (Linux 2.6+ event primitives)
- `getrlimit`, `setrlimit` (resource limits — many programs query these)
- `prctl` (at least PR_SET_NAME for thread naming)
- `sysinfo` (total/free RAM, uptime — used by many tools)

### Step 19.2 — /proc Expansion

- `/proc/self` symlink (critical — almost every program uses it)
- `/proc/[pid]/maps` (memory map — needed by debuggers, sanitizers)
- `/proc/[pid]/exe` symlink to executable
- `/proc/[pid]/fd/` directory with symlinks to open files
- `/proc/[pid]/cmdline`, `/proc/[pid]/environ`
- `/proc/cpuinfo`, `/proc/stat`, `/proc/loadavg`

### Step 19.3 — Terminal Emulation (PTY)

- Implement pseudo-terminals (`/dev/pts/*`)
- `openpty()`, `posix_openpt()`, `grantpt()`, `unlockpt()`
- Line discipline: canonical mode (line buffering), raw mode, echo
- Terminal ioctls: `TIOCGWINSZ`, `TIOCSWINSZ`, `TCGETS`, `TCSETS`, `TIOCSCTTY`
- This is required to run any real interactive program (vim, less, htop, ssh)

### Step 19.4 — Linux Syscall Hardening

- Proper `errno` return convention for all 76+ syscalls (negative return = -errno)
- `strace`-style syscall tracing (log syscall number + args to serial) for debugging
- Stub commonly-queried syscalls that can safely return 0 or ENOSYS
- Goal: `busybox ash` runs as a secondary shell

### Step 19.5 — Win32 Expansion

- `comctl32.dll` shim: common controls (ListView, TreeView, ProgressBar, Toolbar)
- `winmm.dll` shim: `PlaySound`, `waveOutOpen`, `timeGetTime` (map to audio subsystem)
- `version.dll` shim: `GetFileVersionInfo` (many apps check this early)
- `shlwapi.dll` shim: path utilities (`PathCombine`, `PathFindFileName`)
- Goal: run a few more real Win32 apps (simple utilities, early open-source Windows software)

---

## Phase 20 — Power Management & System Polish

**Current state:** ACPI shutdown/reboot only. No suspend, no CPU frequency scaling, no battery awareness.
**Target state:** ~2012 era system management.

### Step 20.1 — ACPI Sleep States

- S1 (power-on suspend): halt CPUs, low-power, quick resume
- S3 (suspend-to-RAM): save state, power down most hardware, resume from RAM
- Implement the ACPI sleep state machine (PM1a/PM1b control registers)
- Resume path: re-init PIC/APIC, restore CPU state, wake scheduler

### Step 20.2 — CPU Idle States (C-States)

- `HLT` instruction in idle loop (C1) — already done implicitly
- ACPI C2/C3 states via `ioport` access for deeper idle
- Reduces power consumption when the OS is idle

### Step 20.3 — Syslog / Kernel Log

- Ring buffer kernel log (`dmesg` command)
- `printk` levels (KERN_ERR, KERN_WARN, KERN_INFO, KERN_DEBUG)
- `/proc/kmsg` or `/dev/kmsg` interface
- User-space `syslog()` writing to `/var/log/messages`
- Log rotation (circular buffer or size-limited file)

### Step 20.4 — Timezone Support

- Parse TZ database (zoneinfo) for UTC offset and DST rules
- `TZ` environment variable
- `localtime()`, `mktime()` in libc that respect timezone
- Display local time in taskbar clock, `date` command, and file timestamps

### Step 20.5 — Init System

- Proper `/sbin/init` as PID 1 (instead of kernel launching shell directly)
- `/etc/inittab` or simple script-based init: mount filesystems, start services, launch login
- `rc.d`-style service scripts or a simple service manager
- Graceful shutdown: signal all processes, sync filesystems, unmount, ACPI power off
- Runlevels or targets: single-user, multi-user, graphical

---

## Phase 21 — Loadable Kernel Modules & FAT32 Write

**Current state:** Monolithic kernel, all drivers compiled in. FAT32 read (from Phase 16).
**Target state:** ~2010 era (modular kernel, interoperable storage).

### Step 21.1 — Module Format & Loader

- Define module format: ELF relocatable `.ko` files
- Module loader: parse ELF sections, resolve symbols against kernel symbol table, apply relocations
- `init_module()` / `cleanup_module()` entry points per module
- Module memory allocation from kernel heap with proper alignment

### Step 21.2 — Kernel Symbol Table Export

- Export key kernel functions: `kmalloc`, `kfree`, `printk`, `register_chrdev`, `register_blkdev`, `spin_lock`, `spin_unlock`
- `EXPORT_SYMBOL()` macro that places entries in a `__ksymtab` section
- Symbol lookup by name at module load time

### Step 21.3 — Module Management

- `insmod`, `rmmod`, `lsmod` shell commands
- Dependency tracking (module A requires module B loaded first)
- Reference counting to prevent unloading in-use modules
- `/proc/modules` listing loaded modules with refcounts

### Step 21.4 — Convert Drivers to Modules

- Move NIC drivers (RTL8139, PCnet) into loadable modules
- Move AC'97 audio driver into a module
- Move USB UHCI + device class drivers into modules
- Keep core built-in: VirtIO GPU, ATA, keyboard, mouse, VirtIO tablet
- Goal: kernel binary shrinks, drivers load on demand

### Step 21.5 — FAT32 Write Support

- Extend the FAT32 driver from read-only to read-write
- File creation, deletion, directory creation
- FAT table updates with proper cluster chain management
- Dirty flag and `sync` support
- Goal: copy files to/from USB drives

---

## Summary — Dependency Graph (Phases 12–21)

```
Phase 12: I/O Mux & Shell ─────────────────────────────────────┐
  12.1 Native poll/select                                       │
  12.2 Shell variables                                          │
  12.3 Redirection & globbing                                   │
  12.4 Control flow                                             │
  12.5 Job control                                              │
  12.6 Core utilities (cp, mv, grep...)                         │
                                                                │
Phase 13: Image & Desktop ─────────────────────────────────────│
  13.1 PNG decoder                                              │
  13.2 JPEG decoder                                             │
  13.3 BMP/ICO support                                          │
  13.4 Image viewer app                                         │
  13.5 Wallpaper & theming                                      │
  13.6 Clipboard                                                │
                                                                │
Phase 14: SMP ◄── hardest phase ───────────────────────────────│
  14.1 APIC / IOAPIC ◄── replaces PIC                          │
  14.2 AP startup (boot secondary cores)                        │
  14.3 SMP-safe kernel (spinlocks everywhere)                   │
  14.4 SMP scheduler (per-CPU runqueues)                        │
  14.5 TLB shootdown                                            │
                                                                │
Phase 15: Networking Maturity ◄── needs 12.1 (poll)            │
  15.1 TCP window scaling & performance                         │
  15.2 Loopback interface                                       │
  15.3 HTTP POST & chunked transfer                             │
  15.4 TLS SNI                                                  │
  15.5 UDP sendto/recvfrom & DNS cache                          │
  15.6 Netcat utility                                           │
                                                                │
Phase 16: USB Device Classes ──────────────────────────────────│
  16.1 USB transfer infrastructure                              │
  16.2 USB HID (keyboard/mouse)                                 │
  16.3 USB mass storage                                         │
  16.4 FAT32 read support ◄── needs VFS                        │
  16.5 USB hub support                                          │
                                                                │
Phase 17: Audio Expansion ◄── needs 16 for nothing, standalone │
  17.1 Audio device interface (/dev/dsp)                        │
  17.2 WAV file playback                                        │
  17.3 Multi-stream mixing                                      │
  17.4 System sounds                                            │
  17.5 DOOM audio                                               │
                                                                │
Phase 18: Security ◄── needs 14 (SMP-aware NX/ASLR)           │
  18.1 NX bit                                                   │
  18.2 Stack canaries                                           │
  18.3 ASLR                                                     │
  18.4 Password hashing                                         │
  18.5 Kernel stack isolation                                   │
                                                                │
Phase 19: Binary Compat Expansion ◄── needs 15, 17, 18        │
  19.1 Missing critical syscalls (epoll, eventfd, etc.)         │
  19.2 /proc expansion                                          │
  19.3 PTY (pseudo-terminals)                                   │
  19.4 Linux syscall hardening                                  │
  19.5 Win32 expansion (comctl32, winmm, etc.)                  │
                                                                │
Phase 20: Power & System Polish ◄── needs 14.1 (APIC)         │
  20.1 ACPI sleep states (S1/S3)                                │
  20.2 CPU idle states (C-states)                               │
  20.3 Syslog / kernel log                                      │
  20.4 Timezone support                                         │
  20.5 Init system                                              │
                                                                │
Phase 21: Modules & FAT32 Write ◄── needs 16.4                │
  21.1 Module format & loader                                   │
  21.2 Kernel symbol table export                               │
  21.3 Module management (insmod/rmmod/lsmod)                   │
  21.4 Convert drivers to modules                               │
  21.5 FAT32 write support                                      │
```

---

## Estimated Timeline (Phases 12–21)

| Phase | Effort | Era Jump |
|-------|--------|----------|
| 12 — I/O Mux & Shell Scripting | 3-4 weeks | Shell goes from ~1993 to ~2005 |
| 13 — Image & Desktop Polish | 3-5 weeks | Desktop goes from ~2003 to ~2010 |
| 14 — SMP | 6-8 weeks | Hardest phase — multi-core support |
| 15 — Networking Maturity | 2-3 weeks | Networking goes from ~2005 to ~2012 |
| 16 — USB Device Classes | 4-5 weeks | Functional USB devices at last |
| 17 — Audio Expansion | 2-3 weeks | Real audio playback with mixing |
| 18 — Security Hardening | 3-4 weeks | Basic exploit mitigations |
| 19 — Binary Compat Expansion | 4-5 weeks | Run real-world Linux utilities |
| 20 — Power & System Polish | 3-4 weeks | Proper init, logging, timezones |
| 21 — Modules & FAT32 Write | 3-4 weeks | Modular kernel, USB interop |

**Total: ~35-45 weeks of focused development**

After all 21 phases, ImposOS would sit comfortably in the **~2013-2015 era** — roughly Linux 3.x / Windows 7 / Mac OS X Mavericks level across all subsystems.

---

## Combined Era Progression

| Phases | Era | Comparable To |
|--------|-----|---------------|
| 1–5 | ~2000-2003 | Linux 2.4, Windows 2000 |
| 6–11 | ~2005-2008 | Linux 2.6, Windows XP SP2, Mac OS X Tiger |
| 12–16 | ~2008-2012 | Linux 2.6.30+, Windows Vista/7, Mac OS X Snow Leopard |
| 17–21 | ~2012-2015 | Linux 3.x, Windows 7 SP1, Mac OS X Mavericks |