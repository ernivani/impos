# ImposOS Modernization Upgrade Plan

> Linear upgrade path to bring ImposOS from ~1997 era to ~2010+ across all subsystems.
> Each phase builds on the previous one. Follow in order.

---

## Phase 1 — Filesystem Overhaul

**Current state:** Custom FS v2, 32MB total, 256 inodes, 8 direct + 1024 indirect blocks, no journaling, ~4MB max file size.
**Target state:** ~2005 era (ext3/HFS+ level).

The filesystem is the foundation — almost every later phase (larger binaries, shared libraries, swap, /proc, /dev, tmpfs) depends on a capable FS layer first.

### Step 1.1 — Increase Disk Geometry

- Raise the block count from 8192 to at least 262,144 (1GB) or more
- Raise the inode count from 256 to at least 4096
- These are just constant changes but will require updating the on-disk format version and any hardcoded limits in `fs.c`

### Step 1.2 — Double/Triple Indirect Blocks

- Add double indirect block pointers (pointer → block of pointers → data blocks)
- This lifts the max file size from ~4MB to ~4GB
- Required before you can handle larger ELF binaries, swap files, or disk images

### Step 1.3 — Journaling

- Implement a write-ahead journal (even a simple metadata-only journal)
- Log metadata operations (inode updates, block allocations, directory changes) before committing
- On crash recovery, replay the journal to restore consistency
- This brings you from FAT-era reliability to ext3-era (2001)

### Step 1.4 — Virtual Filesystem (VFS) Layer

- Abstract the filesystem interface behind a VFS so multiple filesystem types can coexist
- Define a common `struct vfs_ops` with: `mount`, `open`, `read`, `write`, `readdir`, `stat`, `mkdir`, `unlink`
- Your current FS becomes one backend; this enables Step 1.5 and later tmpfs, procfs, devfs

### Step 1.5 — Special Filesystems (procfs, devfs, tmpfs)

- **procfs** (`/proc`): expose per-process info (pid, status, memory maps, fd table) as virtual files — needed for a proper `top`, `ps`, and debugging
- **devfs** (`/dev`): dynamic device node creation instead of hardcoded device entries — needed for hot-pluggable devices later
- **tmpfs**: RAM-backed filesystem for `/tmp` — useful for pipe buffers, IPC, and temporary files without disk I/O

### Step 1.6 — Larger File Support & Sparse Files

- Support files larger than the block count × block size through sparse allocation (don't allocate zero-filled blocks until written)
- Add `ftruncate` and `lseek` beyond current file size
- Needed for swap files (Phase 3) and larger application data

---

## Phase 2 — Process Model & Scheduling

**Current state:** Preemptive multitasking, 16 fds per process, no threads, TSS-based switching, basic signals.
**Target state:** ~2003 era (Linux 2.6 / Windows XP level).

Depends on Phase 1 (VFS, procfs) for exposing process info and handling larger address spaces.

### Step 2.1 — Increase File Descriptor Limit

- Raise from 16 to at least 256 per process
- Switch from a fixed array to a dynamically allocated fd table
- Almost everything downstream (networking, GUI apps, servers) will hit the 16 fd limit

### Step 2.2 — Kernel Threads (kthreads)

- Implement kernel-level threads sharing the same address space but with independent stacks and register state
- Add `clone()` syscall with flags for shared memory, shared fd table, shared signal handlers
- This is a prerequisite for userspace threads (Step 2.3) and is needed for background kernel work (network processing, async I/O)

### Step 2.3 — Userspace Threads (pthreads-style)

- Implement `pthread_create`, `pthread_join`, `pthread_mutex_*`, `pthread_cond_*` in libc
- Map to kernel threads via `clone()`
- Thread-local storage (TLS) support — you already have `set_thread_area` in the Linux syscall layer, extend it
- Required for running real multi-threaded applications and for Phase 7 (Win32 `CreateThread`)

### Step 2.4 — Improved Scheduler

- Replace the current round-robin (implicit from PIT-driven preemption) with a priority-based scheduler
- Add nice values or at least 3-4 priority classes (real-time, normal, background, idle)
- Implement `sleep()`, `nanosleep()`, and timer-based wakeups properly
- O(1) or O(log n) runqueue (simple priority array or a basic CFS-like tree)

### Step 2.5 — Full POSIX Signal Handling

- Add the missing signals: SIGCHLD, SIGSTOP, SIGCONT, SIGALRM, SIGSEGV (with info)
- Implement `sigaction()` with `sa_sigaction` and `siginfo_t`
- Signal queuing for real-time signals
- Needed for proper process group management and job control (Phase 2.6)

### Step 2.6 — Process Groups & Sessions

- Implement process groups (`setpgid`, `getpgid`), sessions (`setsid`), and controlling terminals
- Job control: foreground/background process groups, SIGTSTP (Ctrl+Z), `fg`, `bg` shell builtins
- Required for a proper shell experience and daemon processes

### Step 2.7 — waitpid & Process Lifecycle

- Full `waitpid()` with WNOHANG, WUNTRACED, WCONTINUED
- Proper zombie reaping, orphan reparenting to init
- `exec` family: `execvp`, `execle`, environment variable passing
- This completes the Unix process model

---

## Phase 3 — Memory Management

**Current state:** PMM + VMM with identity mapping, 4KB/4MB pages, flat 256MB kernel space, 64KB shared memory regions.
**Target state:** ~2003 era (proper virtual address spaces).

Depends on Phase 1 (VFS for swap files, larger FS) and Phase 2 (process model for per-process address spaces).

### Step 3.1 — Per-Process Address Spaces

- Each process gets its own page directory
- Map kernel space identically across all processes (upper 1GB or 2GB), user space is private (lower 2GB or 3GB)
- `fork()` with copy-on-write (COW): mark shared pages read-only, copy on page fault
- This is the single biggest architectural leap — it enables true process isolation

### Step 3.2 — Demand Paging

- Don't map all pages at process creation; map them on first access via page fault handler
- Lazy allocation: `mmap` reserves virtual range but only allocates physical frames on fault
- Reduces memory pressure significantly for large address space reservations

### Step 3.3 — mmap (Full Implementation)

- `mmap` with `MAP_PRIVATE`, `MAP_SHARED`, `MAP_ANONYMOUS`, `MAP_FIXED`
- File-backed mappings (read a file by mapping it into memory) — requires VFS (Phase 1.4)
- This is how modern programs load shared libraries, allocate large buffers, and do file I/O

### Step 3.4 — Swap

- Page out least-recently-used frames to a swap file or swap partition
- Basic LRU or clock algorithm for page replacement
- Swap-backed anonymous pages allow the system to overcommit memory
- Requires the filesystem to handle large files (Phase 1.6)

### Step 3.5 — Shared Memory (Proper)

- Replace the current fixed 16-region / 64KB shared memory with `mmap MAP_SHARED`
- Support arbitrary sizes, proper refcounting, CoW semantics
- POSIX `shm_open`/`shm_unlink` backed by tmpfs (Phase 1.5)

---

## Phase 4 — Expanded Syscall Interface

**Current state:** 15 native syscalls, ~20 Linux syscalls.
**Target state:** ~100+ Linux syscalls (enough for musl libc / basic GNU tools).

Depends on Phase 2 (threads, signals, process groups) and Phase 3 (mmap, per-process address spaces).

### Step 4.1 — Core POSIX Syscalls

Add the missing fundamentals. These are needed for nearly any real Unix program:

- `dup`, `dup2` — fd duplication (critical for shell redirection)
- `fcntl` — fd flags, non-blocking I/O
- `access`, `unlink`, `rename`, `rmdir`, `link`, `symlink`, `readlink`
- `chdir`, `fchdir`
- `umask`, `chmod`, `chown` (already partial — complete them)
- `getuid`, `getgid`, `setuid`, `setgid`, `geteuid`, `getegid`
- `time`, `gettimeofday`, `clock_gettime`
- `uname`

### Step 4.2 — Poll / Select / Epoll

- Implement `select()` and `poll()` for I/O multiplexing
- Even a basic `poll()` unlocks event-driven servers, GUI event loops, and terminal multiplexers
- Later: `epoll` for scalable I/O (Linux 2.6 era)
- Depends on the fd table expansion (Phase 2.1)

### Step 4.3 — ioctl Framework

- Generalize `ioctl()` to dispatch to device-specific handlers (terminal, network, DRM, block devices)
- Terminal ioctls: `TIOCGWINSZ`, `TCGETS`, `TCSETS` (for ncurses-style apps)
- Network ioctls: `SIOCGIFADDR`, `SIOCSIFADDR`
- DRM ioctls: already partially there, formalize them

### Step 4.4 — Extended File Operations

- `readv`, `writev` — scatter/gather I/O
- `sendfile` — zero-copy file-to-socket transfer
- `truncate`, `ftruncate`
- `statfs`, `fstatfs` — filesystem info
- `getdents64` improvements (already exists, make it robust)

### Step 4.5 — Futex (Fast User-Space Mutex)

- Implement `futex()` syscall — this is the building block for all userspace synchronization
- pthreads mutexes, condition variables, and semaphores all compile down to futex calls
- Depends on threads (Phase 2.3)

---

## Phase 5 — Dynamic Linking & Shared Libraries

**Current state:** Static ELF32 only, no dynamic linker.
**Target state:** ~2000 era (Linux with ld-linux.so).

Depends on Phase 3 (mmap for library loading, per-process address spaces) and Phase 4 (extended syscalls for the dynamic linker).

### Step 5.1 — ELF Dynamic Loader (ld.so)

- Parse ELF `PT_DYNAMIC` segment, `.dynamic` section
- Resolve `DT_NEEDED` entries to find required shared libraries
- Load `.so` files via `mmap` at their preferred (or relocated) addresses

### Step 5.2 — Symbol Resolution & Relocation

- Process `DT_REL`/`DT_RELA` relocation tables
- Handle `R_386_32`, `R_386_PC32`, `R_386_GLOB_DAT`, `R_386_JMP_SLOT`, `R_386_RELATIVE`
- Lazy binding via PLT/GOT (resolve on first call) for performance

### Step 5.3 — Shared libc (libc.so)

- Compile your libc as a shared library
- All user programs link against it dynamically — saves memory (one copy mapped into all processes)
- This is also the path toward running musl-linked binaries

### Step 5.4 — dlopen / dlsym

- Runtime dynamic loading API
- `dlopen("libfoo.so", RTLD_LAZY)`, `dlsym(handle, "function_name")`
- Enables plugin architectures, optional features, and is used heavily by real applications

---

## Phase 6 — Networking Stack Modernization

**Current state:** Full L2-L7 stack, BSD sockets, TCP/UDP, TLS 1.2, HTTP server, DNS, DHCP. Already strong — this phase is polish.
**Target state:** ~2005 era (Linux 2.6 networking).

Depends on Phase 4 (poll/epoll for async I/O, ioctl framework).

### Step 6.1 — Non-Blocking Sockets & Async I/O

- `O_NONBLOCK` flag on sockets
- `EAGAIN`/`EWOULDBLOCK` return values
- Combine with `poll()`/`select()` (Phase 4.2) for event-driven networking

### Step 6.2 — TCP Improvements

- Implement TCP window scaling (RFC 1323)
- TCP congestion control: at least slow start + congestion avoidance (Reno or NewReno)
- TCP keepalive
- Proper TIME_WAIT handling
- Retransmission timer improvements

### Step 6.3 — Unix Domain Sockets

- `AF_UNIX` / `AF_LOCAL` socket family
- `SOCK_STREAM` and `SOCK_DGRAM` modes
- Heavily used for local IPC (X11, D-Bus, GUI toolkits)
- This enables Phase 8 (IPC infrastructure) to work over sockets

### Step 6.4 — IPv6 (Optional, Low Priority)

- Dual-stack IPv4/IPv6
- Neighbor Discovery Protocol instead of ARP
- This is optional for a hobby OS but would bring you past 2010

---

## Phase 7 — Win32 Compatibility Expansion

**Current state:** PE loader with 11 DLL shims, SEH support. Already impressive.
**Target state:** ~2000 era (Windows 2000 compatibility level).

Depends on Phase 2 (threads for CreateThread), Phase 3 (mmap for VirtualAlloc), Phase 5 (dynamic linking concepts for DLL loading).

### Step 7.1 — Win32 Threading

- Implement `CreateThread`, `ExitThread`, `WaitForSingleObject`, `WaitForMultipleObjects`
- Critical sections: `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`
- Events: `CreateEvent`, `SetEvent`, `ResetEvent`
- Map to kernel threads (Phase 2.2)

### Step 7.2 — Win32 Memory APIs

- `VirtualAlloc`, `VirtualFree`, `VirtualProtect` (map to mmap, Phase 3.3)
- `HeapCreate`, `HeapAlloc`, `HeapFree` (thin wrapper over malloc or a dedicated heap)
- `MapViewOfFile` for memory-mapped files

### Step 7.3 — Win32 File API Expansion

- `CreateFile` with full flags (sharing modes, creation disposition)
- `ReadFile`, `WriteFile` with overlapped I/O stubs
- `FindFirstFile`, `FindNextFile` for directory enumeration
- `GetFileSize`, `SetFilePointer`, `GetFileAttributes`
- `CreateDirectory`, `RemoveDirectory`

### Step 7.4 — Win32 GUI Expansion

- Expand `user32`: more window styles, `WS_CHILD`, `WS_POPUP`, multiple windows
- Message queue: `PostMessage`, `SendMessage`, `GetMessage` with proper blocking
- Basic common controls: static, button, edit, listbox, combobox (separate DLL or inline)
- `gdi32` expansion: more brush/pen types, `BitBlt`, `StretchBlt`, `SetPixel`, `GetPixel`

### Step 7.5 — Win32 DLL Loading

- `LoadLibrary`, `GetProcAddress`, `FreeLibrary`
- Parse PE import tables more robustly (ordinal imports, forwarded exports)
- Delay-load DLL support

---

## Phase 8 — Desktop Environment & IPC

**Current state:** Window manager, widget toolkit, desktop apps, radial launcher.
**Target state:** ~2005 era (GNOME 2 / KDE 3 level IPC and desktop integration).

Depends on Phase 6.3 (Unix domain sockets), Phase 2.3 (threads), Phase 3.5 (shared memory).

### Step 8.1 — Client-Server Window System

- Move from the current in-kernel window manager to a client-server model (like X11 or Wayland)
- Server process owns the compositor and input; client apps connect via Unix domain sockets or shared memory
- This decouples GUI apps from the kernel — crash in an app doesn't bring down the display

### Step 8.2 — IPC Message Bus

- Implement a simple message bus (inspired by D-Bus) over Unix domain sockets
- Named services register on the bus; clients send messages by service name
- Enables: clipboard sharing, drag-and-drop, system notifications, settings sync

### Step 8.3 — Clipboard & Drag-and-Drop

- Implement a clipboard manager as a bus service
- Copy/paste between applications (at least plain text, then images)
- Basic drag-and-drop protocol between windows

### Step 8.4 — Desktop Notifications & System Tray

- Notification daemon: apps send notification messages via the IPC bus
- System tray area in the panel for background app indicators
- Volume control, network status, clock as tray applets

---

## Phase 9 — Audio Subsystem

**Current state:** PC speaker beep only.
**Target state:** ~2003 era (basic sound output like ALSA or early PulseAudio).

Depends on Phase 2.2 (kernel threads for audio mixing), Phase 1.4 (VFS for device nodes).

### Step 9.1 — Sound Card Driver (AC'97 or Intel HDA)

- Implement an AC'97 driver (simpler, well-documented, emulated by QEMU with `-device AC97`)
- Or Intel HDA if you want something more modern (also emulated by QEMU)
- DMA-based audio output with ring buffers

### Step 9.2 — Audio Abstraction Layer

- Create `/dev/dsp` or `/dev/snd/*` device interface
- `open`, `write`, `ioctl` for sample rate, format, channels
- Basic PCM playback (16-bit signed, 44100 Hz, stereo)

### Step 9.3 — Audio Mixer

- Kernel-level or userspace mixer thread that combines audio streams from multiple processes
- Per-stream volume control
- Required for simultaneous audio (system sounds + music + app audio)

### Step 9.4 — Audio in Applications

- Add audio playback to DOOM (it currently has no sound, presumably)
- Simple system sounds (startup, notification, error beep)
- WAV file playback utility

---

## Phase 10 — Loadable Kernel Modules

**Current state:** Monolithic kernel, all drivers compiled in.
**Target state:** ~2000 era (Linux 2.4 module system).

Depends on Phase 5 (ELF loading, symbol resolution) and Phase 1.4 (VFS for sysfs-like interfaces).

### Step 10.1 — Module Format & Loader

- Define a module format (ELF relocatable `.ko` files)
- Module loader: parse ELF, resolve symbols against kernel symbol table, relocate
- `init_module()` / `cleanup_module()` entry points

### Step 10.2 — Kernel Symbol Table Export

- Export key kernel functions (kmalloc, printk, register_device, etc.) in a symbol table
- Modules link against these symbols at load time
- `EXPORT_SYMBOL()` macro or equivalent

### Step 10.3 — Module Management

- `insmod`, `rmmod`, `lsmod` shell commands
- Dependency tracking (module A requires module B)
- Reference counting to prevent unloading in-use modules
- `/proc/modules` via procfs (Phase 1.5)

### Step 10.4 — Convert Drivers to Modules

- Move NIC drivers (RTL8139, PCnet), sound driver (Phase 9), and non-essential drivers into loadable modules
- Keep core drivers (VirtIO GPU, ATA, keyboard/mouse) built-in
- Reduces kernel binary size and enables driver hot-loading

---

## Summary — Dependency Graph

```
Phase 1: Filesystem ──────────────────────────────────┐
  1.1 Disk geometry                                    │
  1.2 Double indirect blocks                           │
  1.3 Journaling                                       │
  1.4 VFS layer ──────────────────────────┐            │
  1.5 procfs / devfs / tmpfs              │            │
  1.6 Large files / sparse                │            │
                                          │            │
Phase 2: Process Model ◄─── needs 1.4, 1.5            │
  2.1 FD limit increase                               │
  2.2 Kernel threads                                   │
  2.3 Userspace threads (pthreads) ◄── needs 2.2      │
  2.4 Scheduler upgrade                                │
  2.5 Full signals                                     │
  2.6 Process groups ◄── needs 2.5                     │
  2.7 waitpid & lifecycle                              │
                                                       │
Phase 3: Memory Management ◄─── needs 1.4, 1.6, 2.x  │
  3.1 Per-process address spaces                       │
  3.2 Demand paging                                    │
  3.3 mmap (full) ◄── needs 3.1, 3.2                  │
  3.4 Swap ◄── needs 1.6, 3.3                         │
  3.5 Shared memory ◄── needs 3.3, 1.5                │
                                                       │
Phase 4: Syscall Expansion ◄─── needs 2.x, 3.x       │
  4.1 Core POSIX syscalls                              │
  4.2 poll / select / epoll ◄── needs 2.1             │
  4.3 ioctl framework                                  │
  4.4 Extended file ops                                │
  4.5 futex ◄── needs 2.3                             │
                                                       │
Phase 5: Dynamic Linking ◄─── needs 3.3, 4.x         │
  5.1 ELF dynamic loader                              │
  5.2 Symbol resolution                                │
  5.3 Shared libc                                      │
  5.4 dlopen / dlsym                                   │
                                                       │
Phase 6: Networking ◄─── needs 4.2, 4.3              │
  6.1 Non-blocking sockets                             │
  6.2 TCP improvements                                 │
  6.3 Unix domain sockets                              │
                                                       │
Phase 7: Win32 Expansion ◄─── needs 2.2, 3.3, 5.x   │
  7.1 Threading                                        │
  7.2 Memory APIs                                      │
  7.3 File APIs                                        │
  7.4 GUI expansion                                    │
  7.5 DLL loading                                      │
                                                       │
Phase 8: Desktop & IPC ◄─── needs 6.3, 2.3, 3.5     │
  8.1 Client-server window system                      │
  8.2 IPC message bus                                  │
  8.3 Clipboard & DnD                                  │
  8.4 Notifications & tray                             │
                                                       │
Phase 9: Audio ◄─── needs 2.2, 1.4                   │
  9.1 Sound card driver                                │
  9.2 Audio device layer                               │
  9.3 Mixer                                            │
  9.4 App integration                                  │
                                                       │
Phase 10: Modules ◄─── needs 5.x, 1.5               │
  10.1 Module loader                                   │
  10.2 Symbol table                                    │
  10.3 Management                                      │
  10.4 Convert drivers                                 │
```

---

## Estimated Timeline (Rough)

| Phase | Effort | Result |
|-------|--------|--------|
| 1 — Filesystem | 3-5 weeks | OS era jumps from ~1993 to ~2001 |
| 2 — Process Model | 4-6 weeks | ~2003 process management |
| 3 — Memory Management | 6-8 weeks | Hardest phase — proper virtual memory |
| 4 — Syscalls | 3-4 weeks | Unlocks real Unix software |
| 5 — Dynamic Linking | 4-6 weeks | Shared libraries, smaller binaries |
| 6 — Networking | 2-3 weeks | Already strong, mostly polish |
| 7 — Win32 Expansion | 4-6 weeks | Run more Windows programs |
| 8 — Desktop & IPC | 5-7 weeks | Modern desktop architecture |
| 9 — Audio | 3-4 weeks | Sound output, DOOM with audio |
| 10 — Modules | 3-4 weeks | Modular kernel architecture |

**Total: ~40-55 weeks of focused development**

After all 10 phases, ImposOS would sit comfortably in the **2005-2008 era** across all subsystems — roughly Linux 2.6 / Windows XP SP2 / Mac OS X Tiger level.
