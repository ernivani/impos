# ImposOS Acceleration Strategy: Technology Leapfrogging

## Philosophy

> **Don't build what you can skip. Don't port what you can run unchanged.**

Traditional OS development: build POSIX → port libc → cross-compile each tool → run ported binaries.
That's the path SerenityOS, Sortix, and Managarm took. It works, but it's slow.

**Our leapfrog:** Implement the Linux i386 syscall ABI directly. Then pre-built static
Linux binaries run on ImposOS WITHOUT modification, WITHOUT porting, WITHOUT cross-compilation.
Download a binary, copy it to the disk image, run it.

This is what WSL1 (Windows Subsystem for Linux) did — Microsoft didn't port Linux software,
they made the NT kernel speak Linux's language. We do the same thing, but we're smaller
and faster because we only need ~40 syscalls, not 400.

---

## The Leapfrog Map

### What We Skip vs. What We Use Instead

| Traditional Work | Time | Leapfrog Replacement | Time | Savings |
|---|---|---|---|---|
| Design custom POSIX syscall numbers | ~1 week | Use Linux i386 syscall numbers directly | 0 days | 1 week |
| Port musl libc to ImposOS | ~2 weeks | Run pre-built static musl binaries unchanged | 0 days | 2 weeks |
| Implement fork() with COW paging | ~1 week | vfork()+exec(), defer real fork | ~1 day | 6 days |
| Build a dynamic linker (ld.so) | ~2 weeks | Static binaries only (forever, or until needed) | 0 days | 2 weeks |
| Cross-compile each tool individually | ~3 weeks | Download 611 pre-built static i386 binaries | ~1 hour | 3 weeks |
| Port BusyBox (configure, build, debug) | ~1 week | Download 1MB static BusyBox binary | 0 days | 1 week |
| Write custom rendering engine | ~2 weeks | doomgeneric (5 callbacks) + ported freetype2 | ~2 days | 12 days |
| Write custom TrueType font engine | ~2 weeks | Port freetype2 (static binary) | ~1 day | 13 days |
| Write custom HTTP client library | ~1 week | Download static curl binary | 0 days | 1 week |
| Write custom HTML renderer | ~3 weeks | NetSurf framebuffer frontend (raw memory buffer) | ~3 days | 18 days |
| Write custom display protocol | ~4 weeks | Port TinyX/Xfbdev (~950KB X11 server) | ~1 week | 3 weeks |
| Write custom widget toolkit | ~4 weeks | X11 + dwm + existing X11 apps | ~3 days | 25 days |
| Build custom OpenGL renderer | ~3 weeks | Port TinyGL (~4000 LOC) or skip entirely | ~2 days | 19 days |
| Port tools one-by-one | ~3 weeks | BusyBox = 300+ tools in 1 binary | 0 days | 3 weeks |
| Write custom package manager | ~1 week | wget + tar (both from BusyBox) | 0 days | 1 week |

**Total traditional effort: ~35 weeks.  Leapfrog effort: ~3 weeks.  Savings: ~32 weeks.**

---

## The Two-Track Strategy

### Track A: "Native" (compile directly for ImposOS)

Things we compile with i686-elf-gcc and link into the kernel or run as native binaries.
These give the fastest, most impressive results because they bypass all compatibility layers.

**Track A targets:**
- **doomgeneric** — 5 callback functions → Doom runs natively
- **TinyGL** — 4000 LOC software OpenGL → 3D demos
- **Lua interpreter** — small, easy to port, gives scripting
- **Native ImposOS apps** — shell, file manager, settings, etc. (already done)

### Track B: "Linux Compat" (run pre-built Linux binaries)

Implement ~40 Linux i386 syscalls so pre-built static musl binaries run unchanged.
This gives the massive software ecosystem without any porting work.

**Track B targets (pre-built static binaries):**
- **BusyBox** — 300+ Unix utilities (1MB binary)
- **bash** — full-featured shell
- **curl** — HTTP/HTTPS client
- **git** — version control
- **python3** — scripting ecosystem
- **vim/nano** — text editors
- **htop** — process viewer
- **Links/w3m** — text-mode web browsers
- **GCC + binutils** — compile software ON ImposOS
- **NetSurf** — GUI web browser (framebuffer mode)

Both tracks run in parallel. Track A gives quick visual wins (Doom!), Track B builds the ecosystem.

---

## The Filesystem Bottleneck (Must Solve First)

**Current limitation:** 64 inodes, 256 × 512B blocks = 128KB total storage.
BusyBox alone is 1MB. We need at least 32MB.

### Solution: GRUB Multiboot Module as Initrd

**The leapfrog:** GRUB can load arbitrary files as multiboot modules and pass their
address to the kernel. This is exactly how Linux's initramfs works.

```
                    GRUB loads:
                    ├── kernel.elf (the OS)
                    └── initrd.tar (packed filesystem image, 32MB+)
                          ├── bin/busybox (1MB static binary)
                          ├── bin/bash
                          ├── bin/curl
                          ├── usr/bin/doom (native)
                          └── ...
```

**Implementation:**
1. Parse multiboot info for module address + size (~20 lines)
2. Parse tar headers to find files (~100 lines, tar format is trivial)
3. Mount as a read-only overlay on top of existing FS
4. Total: ~150 lines of code, done in half a day

**Alternative (backup):** Expand existing FS to 4KB blocks, 8192 blocks = 32MB, 256 inodes.
Requires disk format change + `make clean-disk`.

Both can coexist: initrd for pre-loaded Linux binaries, existing FS for user files.

---

## Phase Plan: Day by Day

### Phase 0: Doom Day (Day 1-2) — Track A
_The single most impressive milestone. No Linux compat needed._

**doomgeneric** requires exactly 5 functions:
```c
void DG_Init(int argc, char **argv);           // → your gfx_init() already done
void DG_DrawFrame(void);                        // → memcpy DG_ScreenBuffer to framebuffer
void DG_SleepMs(uint32_t ms);                   // → pit_sleep_ms() exists
uint32_t DG_GetTicksMs(void);                   // → pit_get_ticks() * 8 exists
int DG_GetKey(int *pressed, unsigned char *key); // → keyboard ring buffer exists
```

**Work:**
1. Download doomgeneric source
2. Add to kernel build as an app (like shell.c, vi.c)
3. Implement 5 callbacks mapping to existing APIs
4. Bundle shareware DOOM1.WAD in filesystem (or initrd)
5. Add `doom` command to shell

**Result:** Doom runs on ImposOS. Screenshots go viral on r/osdev.

### Phase 1: Static ELF Hello World (Day 3-4) — Track B
_Prove Linux binary compat works._

**Implement 6 Linux syscalls:**
| # | Syscall | Linux i386 # | Implementation |
|---|---------|-------------|----------------|
| 1 | `write` | 4 | Route to tty_write or pipe_write |
| 2 | `writev` | 146 | Loop calling write for each iovec |
| 3 | `exit_group` | 252 | Route to task_exit |
| 4 | `brk` | 45 | Move per-process heap pointer |
| 5 | `mmap2` | 192 | Allocate anonymous pages, map into PD |
| 6 | `set_thread_area` | 243 | Write user_desc to GDT, set %gs |

**Plus:**
- Static ELF32 loader: parse headers, map PT_LOAD segments, jump to e_entry
- Remap INT 0x80 handler to dispatch Linux syscall numbers

**Test:** Cross-compile or download a static musl hello world. It prints "Hello, world!".

### Phase 2: BusyBox Day (Day 5-8) — Track B
_300+ Unix tools in one shot._

**Add ~15 more Linux syscalls:**
| Syscall | # | Maps to |
|---------|---|---------|
| `open` | 5 | fs_open_file → new kernel fd table |
| `close` | 6 | fd_close |
| `read` | 3 | fs_read_file / pipe_read |
| `stat64` | 195 | fs_stat → populate Linux stat64 struct |
| `fstat64` | 197 | same via fd |
| `ioctl` | 54 | Terminal: TIOCGWINSZ, TCGETS/TCSETS |
| `fcntl64` | 221 | F_GETFD, F_SETFD, F_GETFL, F_SETFL |
| `getdents64` | 220 | fs_readdir → Linux dirent64 format |
| `getcwd` | 183 | Return cwd path |
| `uname` | 122 | Return "ImposOS" sysname |
| `getuid32` | 199 | user_get_current_uid() |
| `getgid32` | 200 | user_get_current_gid() |
| `geteuid32` | 201 | same (no seteuid yet) |
| `getegid32` | 202 | same |
| `lseek` | 19 | Per-fd file offset |

**Also:** Filesystem expansion (initrd or bigger disk) to hold BusyBox + files.

**Test:** Boot ImposOS, type `busybox sh`, get a BusyBox shell with ls, cat, grep, sed, awk,
find, tar, gzip, wget, vi, and 290+ more utilities.

### Phase 3: Process Management (Day 9-13) — Track B
_Run commands from the shell. Pipelines. Job control basics._

**Add ~12 more syscalls:**
| Syscall | # | Difficulty | Notes |
|---------|---|-----------|-------|
| `clone` | 120 | Medium | Implement as vfork (share parent memory until exec) |
| `execve` | 11 | Medium | Load new ELF into existing process, reset state |
| `wait4` | 114 | Easy | Block parent until child exits, return status |
| `pipe` | 42 | Easy | Already have pipes, just assign Linux fd numbers |
| `dup2` | 63 | Easy | Copy fd table entry |
| `rt_sigaction` | 174 | Easy | Map to existing signal infrastructure |
| `rt_sigprocmask` | 175 | Medium | Add signal mask to task_info_t |
| `rt_sigreturn` | 173 | Easy | Already have sigreturn |
| `kill` | 37 | Easy | Already have sig_send_pid |
| `getpid` | 20 | Trivial | Already exists |
| `getppid` | 64 | Easy | Add parent_pid to task_info_t |
| `nanosleep` | 162 | Easy | Map to pit_sleep_ms |

**KEY LEAPFROG: Skip fork(), use vfork semantics for clone().**
- vfork shares parent's address space until exec (no page copying)
- 90% of shell usage is fork+exec, which vfork handles perfectly
- Real COW fork can come later for programs that actually need it

**Test:** BusyBox ash runs commands: `ls | grep foo`, `cat file`, `echo hello > file`.
Bash static binary also works for interactive use.

### Phase 4: Networking (Day 14-16) — Track B
_Existing TCP/IP stack, just needs Linux socket syscall wrappers._

**Add socket syscalls (via socketcall multiplexer or individual):**
| Syscall | # | Maps to |
|---------|---|---------|
| `socketcall` | 102 | Multiplex: socket/bind/listen/accept/connect/send/recv/etc. |
| `socket` | — | Map to ImposOS socket_create |
| `connect` | — | Map to ImposOS tcp_connect |
| `send/sendto` | — | Map to tcp_send / udp_send |
| `recv/recvfrom` | — | Map to tcp_recv / udp_recv |
| `select` | 142 | Implement fd-set polling over sockets+pipes+files |

**Note:** On i386 Linux, most socket ops go through `socketcall(SYS_SOCKET, args)` (syscall 102)
rather than individual syscall numbers. This is a single dispatch function.

**Test:** Static curl downloads a web page. Static wget fetches a file.
`busybox wget http://example.com` works.

### Phase 5: Terminal & PTY (Day 17-19) — Track B
_Make terminal apps work properly._

**Implement:**
- Pseudo-terminals (PTY): `openpty` / `posix_openpt` / `grantpt` / `unlockpt`
- Terminal ioctls: `TCGETS`, `TCSETS`, `TIOCGWINSZ`, `TIOCSWINSZ`
- `/dev/tty`, `/dev/null`, `/dev/zero`, `/dev/urandom` device nodes
- Line discipline: raw mode, cooked mode, echo

**Test:** vim, nano, htop, tmux all work in the ImposOS terminal.
Python3 REPL works with line editing.

### Phase 6: NetSurf Browser (Day 20-25) — Track A+B Hybrid
_A real web browser with CSS and JavaScript. On your OS._

**Strategy:** NetSurf has a "ram" framebuffer surface that renders to a raw memory buffer.
We write a thin ImposOS backend (~200 lines) that:
1. Creates a WM window for the browser
2. Points NetSurf's framebuffer output at the window's pixel buffer
3. Routes mouse/keyboard events from WM to NetSurf's input handler

**Dependencies (cross-compile as static):**
- NetSurf internal libs (libcss, hubbub, etc.) — bundled with NetSurf source
- libcurl (for HTTP) — already have the network stack
- libpng (for images) — ~60KB static lib
- freetype2 (for fonts) — ~400KB static lib

**Alternative fast path:** Static `links -g` (graphical Links browser) renders to
framebuffer directly. Simpler deps, still gives a real graphical browser.

**Test:** Browse real websites on ImposOS. Render HTML, CSS, images.

### Phase 7: X11 — The Mega Leapfrog (Day 26-35)
_One port that unlocks EVERY X11 application ever written._

**Port TinyX/Xfbdev (~950KB):**
- Minimal X11 server that draws to a framebuffer
- ImposOS backend: point at our framebuffer, route PS/2 input
- Needs: `mmap`, `select`, `socket` (Unix domain), `ioctl` — all from earlier phases

**Port dwm (~30KB):**
- Tiling window manager, 2000 lines of C
- Depends only on libX11 (~1.5MB)

**Port st (~50KB):**
- suckless terminal emulator
- Runs inside X11, provides terminal for all CLI apps

**Result:** Full X11 desktop. Launch xterm, run vim, browse with surf, tile windows with dwm.
Every X11 application from the last 40 years becomes available.

### Phase 8: Self-Hosting (Day 36-45)
_Build software ON ImposOS._

By this point, from Phase 2-3, we already have:
- Static GCC binary (from pre-built i386 collection)
- Static make, binutils, as, ld
- bash + full coreutils (from BusyBox or static builds)

**What's left:**
1. Verify GCC can compile + link a hello world ON ImposOS
2. Compile doomgeneric ON ImposOS
3. Compile the ImposOS kernel ON ImposOS (the ultimate milestone)
4. Set up autotools (m4, autoconf, automake, pkg-config)

---

## The "30 Syscalls That Change Everything"

These are ordered by unlock value. Each row shows what becomes possible:

| # | Syscall | Linux i386 # | What it unlocks |
|---|---------|-------------|-----------------|
| 1 | write | 4 | Any program can print output |
| 2 | exit_group | 252 | Programs can exit cleanly |
| 3 | brk | 45 | malloc works (musl heap) |
| 4 | mmap2 | 192 | Large allocations, stack setup |
| 5 | set_thread_area | 243 | TLS works → musl initializes → ANY musl program |
| 6 | writev | 146 | printf works (musl's stdout uses writev) |
| 7 | read | 3 | Programs can read input |
| 8 | open | 5 | Programs can open files |
| 9 | close | 6 | File cleanup |
| 10 | fstat64 | 197 | stat() on open files |
| 11 | stat64 | 195 | stat() by path → ls works |
| 12 | getdents64 | 220 | readdir → ls can list directories |
| 13 | ioctl | 54 | Terminal control → interactive programs |
| 14 | getcwd | 183 | pwd, shell prompt |
| 15 | uname | 122 | System identification |
| 16 | getuid32/getgid32 | 199/200 | User identity |
| — | — | — | **BusyBox runs (300+ tools)** |
| 17 | clone (as vfork) | 120 | Process creation without COW |
| 18 | execve | 11 | Shell can launch programs |
| 19 | wait4 | 114 | Shell waits for programs to finish |
| 20 | pipe | 42 | Shell pipelines work |
| 21 | dup2 | 63 | I/O redirection works |
| 22 | rt_sigaction | 174 | Signal handlers (Ctrl+C in bash) |
| 23 | rt_sigprocmask | 175 | Signal masking |
| 24 | kill | 37 | Send signals between processes |
| 25 | nanosleep | 162 | sleep command, timing |
| — | — | — | **bash + interactive shell works** |
| 26 | socketcall | 102 | All socket ops → networking |
| 27 | select/poll | 142/168 | Multiplexed I/O → servers, clients |
| 28 | chdir | 12 | cd command |
| 29 | access | 33 | Permission checks |
| 30 | mprotect | 125 | Memory protection → JIT, security |
| — | — | — | **curl, wget, git, python work** |

---

## Permanent Skips (Things We Never Build)

| Never Build | Use Instead | Why |
|---|---|---|
| Custom TrueType engine | Ported freetype2 | 30 years of font rendering expertise |
| Custom HTTP library | Static curl | HTTP/2, TLS 1.3, proxies, cookies, everything |
| Custom HTML/CSS renderer | NetSurf (framebuffer mode) | Real browser, real CSS, real JavaScript |
| Custom OpenGL renderer | TinyGL (4000 LOC) or skip | Port Mesa softpipe later if needed |
| Custom display protocol | X11 (TinyX/Xfbdev) | 40 years of application ecosystem |
| Custom widget toolkit for ports | GTK2/FLTK via X11 | Thousands of existing apps |
| Custom C library for ports | Pre-built musl (static) | Runs unchanged, no porting needed |
| Custom package manager | BusyBox wget + tar | Or port opkg (tiny embedded pkg manager) |
| Custom assembler | Static GNU as | Battle-tested, full x86 support |
| Custom linker | Static GNU ld | Same |
| Individual tool porting | BusyBox (300+ in 1 binary) | One binary, one megabyte, done |
| Dynamic linker (initially) | Static binaries only | Skip PLT/GOT/ld.so complexity entirely |
| COW fork (initially) | vfork+exec pattern | 90% of fork usage is fork+exec |

---

## Realistic End State by Week

| Week | What runs on ImposOS | Cumulative syscalls |
|---|---|---|
| 0 (now) | Win32 apps, native shell, GUI desktop | 15 (custom) |
| 1 | + Doom (native) + static Linux hello world | +6 Linux |
| 2 | + BusyBox (300+ tools), basic file I/O | +10 Linux |
| 3 | + bash, process management, pipes, signals | +12 Linux |
| 4 | + curl, wget, networking apps | +3 Linux |
| 5 | + vim, nano, htop, tmux, python3, terminals | +5 pty/terminal |
| 6 | + NetSurf browser (framebuffer), web browsing | cross-compiled |
| 7-8 | + X11 desktop (TinyX + dwm + st + xterm) | +2-3 Unix socket |
| 9-10 | + GCC compiling on ImposOS, self-hosting | existing suffice |

**Total: ~45 Linux syscalls to reach full ecosystem.**

---

## Risk Assessment

| Risk | Mitigation |
|---|---|
| set_thread_area is complex on i386 | Already have GDT infrastructure + TIB; ~50 lines of code |
| Static binaries expect /dev/* | Implement minimal devfs: /dev/null, /dev/zero, /dev/tty, /dev/urandom |
| FS too small for binaries | Initrd via GRUB multiboot module; or expand FS to 32MB |
| Some binaries need mmap with MAP_SHARED | Stub as MAP_PRIVATE (copy semantics); X11 SHM can come later |
| Terminal apps need proper pty | Implement minimal pty pair + line discipline (~300 lines) |
| 32 task limit too low for X11 | Increase MAX_TASKS to 128 (change constant + enlarge array) |
| 256MB RAM limit | Sufficient for static binaries; X11 + dwm + apps fits in ~64MB |

---

## Summary: The Leapfrog Sequence

```
Day 1-2:    DOOM RUNS (native, zero Linux compat needed)
Day 3-4:    Static Linux hello world runs (6 syscalls)
Day 5-8:    BusyBox runs — 300+ Unix tools (16 more syscalls)
Day 9-13:   bash + process management (12 more syscalls)
Day 14-16:  Networking — curl, wget work (socket syscalls)
Day 17-19:  Terminal — vim, htop, tmux, python work (pty)
Day 20-25:  NetSurf — real web browser on ImposOS
Day 26-35:  X11 — EVERY X11 app from the last 40 years
Day 36-45:  Self-hosting — GCC compiles on ImposOS

Total new kernel code: ~3000-5000 lines (syscall handlers + ELF loader)
Total new applications: THOUSANDS (pre-built, downloaded, unchanged)
```

**This is the leapfrog. We don't build the software. We build the 3000 lines of kernel
code that lets us run everyone else's software.**
