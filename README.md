# ImposOS

A 32-bit x86 operating system built from scratch in C, featuring a full graphical desktop environment, GPU-accelerated compositing via VirtIO GPU 3D (virgl), a complete TCP/IP networking stack with TLS 1.2, a Unix-style filesystem, and Win32/Linux binary compatibility layers.

~130K lines of kernel + libc code. Boots via GRUB multiboot, runs in QEMU or on bare metal.

## Features

### Desktop Environment
- **1920x1080 32-bit color** desktop with window manager (z-ordering, resize, minimize, maximize, drag)
- **GPU-accelerated compositing** via raw Gallium3D/virgl command encoding over VirtIO GPU 3D
- **Software compositor fallback** when GPU acceleration is unavailable
- **Widget toolkit** with buttons, labels, text inputs, sliders, checkboxes, lists, progress bars
- **Theme engine** with GNOME-dark inspired styling
- **Desktop apps**: File Manager, Terminal, Task Manager, Settings, System Monitor
- **Radial launcher menu**, context menus, dock, menu bar
- **TrueType font rendering** and vector path graphics
- **DRM/KMS** kernel subsystem (CRTC/connector/encoder model, GEM buffers, page flip)

### GPU Pipeline
- **VirtIO GPU 2D**: framebuffer scanout, hardware cursor, dirty rectangle updates
- **VirtIO GPU 3D (virgl)**: full Gallium3D command submission — no Mesa dependency
  - TGSI text shaders (vertex + fragment)
  - Textured quad rendering with per-surface alpha blending
  - Blend, rasterizer, DSA, sampler, vertex element state objects
  - Per-surface 3D texture resources with PMM-backed storage
  - Readback-based presentation (TRANSFER_FROM_HOST_3D to backbuffer)
- **DRM subsystem**: mode setting (KMS), GEM buffer management, dumb buffers, page flip
- **Fallback chain**: virgl 3D -> VirtIO 2D -> Bochs VGA (BGA)

### Networking (Full L2-L7 Stack)
- **L2**: RTL8139 and PCnet-FAST III NIC drivers, ARP resolution
- **L3**: IPv4 routing, ICMP (ping)
- **L4**: TCP (reliable streams), UDP (datagrams)
- **L5-7**: DHCP client, DNS resolver, HTTP server, TLS 1.2
- **BSD socket API**: socket, bind, listen, connect, send, recv
- **Stateless firewall**: 16 rules, first-match-wins

### Filesystem
- Custom inode-based filesystem (FS v2)
- 4KB blocks, 8192 blocks (32MB), 256 inodes
- 8 direct + 1024 indirect block pointers (~4MB max file size)
- Unix permissions (rwx owner/group/other), symlinks, device nodes
- Character devices: `/dev/null`, `/dev/zero`, `/dev/tty`, `/dev/urandom`, `/dev/dri/card0`
- Per-user disk quotas, dirty bitmap for efficient sync
- initrd (TAR) mounted at boot

### Process Management
- Preemptive multitasking (120Hz PIT timer)
- Ring 0/Ring 3 privilege separation with dual stacks (kernel + user)
- TSS-based context switching
- 15 native syscalls (INT 0x80): read, write, open, close, pipe, kill, fork, exec, etc.
- POSIX signals: SIGINT, SIGTERM, SIGKILL, SIGUSR1/2, SIGPIPE
- Pipes (4KB circular buffer, blocking I/O)
- Named shared memory (16 regions, 64KB each, mapped at 0x40000000+)
- Per-process file descriptor table (16 fds)

### Binary Compatibility

**Win32 PE executables** — loads and runs .exe files with 11 DLL shim libraries:

| DLL | Coverage |
|-----|----------|
| kernel32 | Process, memory, file I/O, console |
| user32 | CreateWindow, MessageBox, message pump |
| gdi32 | DC, bitmaps, fonts, drawing primitives |
| msvcrt | C runtime (printf, malloc, file ops) |
| advapi32 | Registry, crypto, service stubs |
| ws2_32 | Winsock networking |
| ole32 | COM basics (CoInitialize) |
| shell32 | ShellExecute, SHGetFolderPath |
| bcrypt | BCryptOpenAlgorithmProvider |
| crypt32 | X.509 certificate parsing |
| gdiplus | GDI+ image basics |

SEH (Structured Exception Handling) support included.

**Linux ELF binaries** — static ELF32 execution with ~20 Linux syscalls:
- Process: exit, getpid, fork, execve
- I/O: read, write, open, close, lseek, ioctl
- Memory: mmap2, munmap, brk
- Files: stat64, fstat64, getdents64, getcwd
- TLS: set_thread_area

### Cryptography
- AES-128 (CBC mode)
- SHA-256, HMAC-SHA256
- RSA 2048-bit (PKCS#1 v1.5, via bignum arithmetic)
- Elliptic curve operations (point add, scalar multiply)
- ASN.1/X.509 certificate parsing
- CSPRNG (seeded from PIT + RTC + RDTSC)
- TLS 1.2 PRF (P_SHA256 key derivation)

### Shell
63 built-in commands with pipe support (`cmd1 | cmd2`):

| Category | Commands |
|----------|----------|
| Core | help, man, echo, cat, ls, cd, touch, clear, pwd, history, mkdir, rm, vi |
| System | sync, exit, logout, shutdown, setlayout, timedatectl, env, export, display |
| Network | ifconfig, ping, arp, nslookup, dhcp, httpd, connect, firewall, ntpdate |
| Users | whoami, su, sudo, id, useradd, userdel |
| Files | chmod, chown, ln, readlink, quota |
| Process | spawn, kill, top, status, shm |
| Debug | test, gfxdemo, gfxbench, fps, drmtest, fstest, proctest, threadtest, memtest, petest |
| Apps | doom, virgl_test, run, winget, beep, lspci |

### Other
- Multi-layout keyboard (US QWERTY + FR AZERTY)
- PS/2 mouse + VirtIO tablet (absolute positioning) input
- ACPI power management (shutdown, reboot)
- RTC (real-time clock), NTP time sync
- PCI bus enumeration
- ATA/IDE disk driver
- PC speaker audio
- DOOM (full game port, 57K LOC)

## Building

### Prerequisites

- i686-elf cross-compiler toolchain (`i686-elf-gcc`, `i686-elf-ld`, etc.)
- QEMU with SDL and OpenGL support (`qemu-system-i386`)
- GRUB tools (`grub-mkrescue`, `xorriso`)
- GNU Make

Install the cross-compiler to `~/opt/cross/bin/` or adjust `PATH`.

### Build & Run

```bash
make              # Build kernel + initrd
make run          # Run with VirtIO GPU (2D), 4GB RAM
make run-gl       # Run with virgl 3D GPU acceleration
make run-gl-sw    # Run with software GL (llvmpipe)
make terminal     # Run in text-only mode (no GUI)
make clean        # Remove build artifacts
```

### QEMU Configuration

The default `make run-gl` launches:

```
qemu-system-i386
  -kernel kernel/myos.kernel
  -initrd initrd.tar
  -device virtio-vga-gl -display sdl,gl=on
  -device virtio-tablet-pci
  -device rtl8139,netdev=net0
  -netdev user,id=net0
  -m 4G
  -enable-kvm -cpu host
```

KVM acceleration is auto-detected. Display backend defaults to SDL on WSL2, GTK on native Linux.

## Architecture

```
kernel/
  kernel/kernel.c              Main entry, subsystem init, state machine
  include/kernel/              All kernel headers
  arch/i386/
    boot.S                     Multiboot entry, protected mode setup
    idt.c                      GDT, IDT, PIC, PIT (interrupt infrastructure)
    isr_stubs.S                ISR/IRQ assembly trampolines
    tty.c                      Serial + VGA text output
    drivers/
      virtio_gpu.c             VirtIO GPU 2D driver (1063 LOC)
      virtio_gpu_3d.c          VirtIO GPU 3D / virgl commands (272 LOC)
      virtio_gpu_drm.c         DRM backend for VirtIO GPU (296 LOC)
      drm_core.c               DRM/KMS subsystem (692 LOC)
      libdrm.c                 libdrm-compatible API (382 LOC)
      virtio_input.c           VirtIO tablet input (466 LOC)
      rtl8139.c / pcnet.c      Network interface drivers
      ata.c                    IDE disk driver
      pci.c                    PCI bus enumeration
      acpi.c                   ACPI power management
      mouse.c                  PS/2 mouse driver
      rtc.c                    Real-time clock
    gui/
      gpu_compositor.c         GPU-accelerated virgl compositor (870 LOC)
      compositor.c             Software compositor + GPU dispatch (572 LOC)
      wm2.c                    Window manager (845 LOC)
      gfx.c                    Framebuffer graphics engine (1480 LOC)
      gfx_ttf.c                TrueType font renderer (776 LOC)
      gfx_path.c               Vector path graphics (523 LOC)
      ui_*.c                   Widget toolkit, theme, events, layout
      desktop.c / login.c      Desktop chrome, login screen
      filemgr.c / taskmgr.c    Desktop applications
      settings.c / monitor.c   System apps
    net/
      net.c                    Driver abstraction layer
      arp.c / ip.c             L2-L3 protocols
      tcp.c / udp.c            L4 transport
      dns.c / dhcp.c           Application protocols
      httpd.c                  HTTP server
      tls.c                    TLS 1.2 (1011 LOC)
      socket.c                 BSD socket API
      firewall.c               Stateless packet filter
    crypto/
      aes.c / sha256.c         Symmetric crypto + hashing
      rsa.c / ec.c / bignum.c  Public key cryptography
      hmac.c / prng.c          HMAC + CSPRNG
      asn1.c                   ASN.1/X.509 parsing
    sys/
      fs.c                     Filesystem (1341 LOC)
      task.c / sched.c         Task management + scheduler
      pmm.c / vmm.c            Physical + virtual memory managers
      syscall.c                Native syscall dispatch
      pipe.c / signal.c / shm.c  IPC mechanisms
      user.c / group.c         User/group management
      pe_loader.c              Win32 PE binary loader
      elf_loader.c             Linux ELF binary loader
      linux_syscall.c          Linux syscall emulation (902 LOC)
      win32_*.c                Win32 API shim libraries (11 DLLs)
    app/
      shell.c                  Shell with 63 commands (5223 LOC)
      vi.c                     Text editor
      virgl_test.c             VirtIO GPU 3D test harness
      doom/                    DOOM port (57K LOC)
libc/
  stdio/ stdlib/ string/       Standard C library
```

## GPU Compositor Internals

The GPU compositor (`gpu_compositor.c`) bypasses Mesa/OpenGL entirely and encodes raw Gallium3D commands directly into the virgl protocol. This approach avoids the complexity of cross-compiling Mesa while achieving the same result: GPU-accelerated textured quad compositing with alpha blending.

**Pipeline setup** (once at init):
1. Create virgl context (ctx_id=2, separate from DRM)
2. Allocate render target (1920x1080 BGRA texture) + vertex buffer (PIPE_BUFFER)
3. Create and bind state objects: blend (src_alpha/inv_src_alpha), rasterizer (no cull, depth_clip, half_pixel_center), DSA (no depth test), vertex elements (float2 pos + float2 uv)
4. Create TGSI text shaders: VS (passthrough position + texcoord), FS (TEX sampler)
5. Create sampler state (nearest filter, clamp-to-edge)
6. Self-test: clear to blue, readback, verify pixel values

**Per-frame render loop**:
1. Upload dirty surface pixels to GPU textures (memcpy to PMM, TRANSFER_TO_HOST_3D)
2. Build vertex buffer: 6 vertices per quad (2 triangles), NDC coords + UVs
3. Encode commands: set framebuffer, viewport, clear to desktop background color
4. For each visible surface (back-to-front layer order): bind sampler view, draw 6 vertices
5. Submit command stream (SUBMIT_3D)
6. Readback rendered pixels (TRANSFER_FROM_HOST_3D)
7. Copy to backbuffer with Y-flip (GL convention), present via 2D scanout

**Fallback**: when virgl is unavailable (`-vga std`), the software compositor handles blitting with CPU-based alpha blending. The dispatch happens transparently in `compositor.c`.

## Memory Map

```
0x00000000 - 0x000FFFFF   Low memory (BIOS, VGA, multiboot structures)
0x00100000 - 0x0FFFFFFF   Kernel + heap (identity-mapped, first 256MB)
0x10000000 - 0x3FFFFFFF   PMM-managed frames (identity-mapped)
0x40000000 - 0x4000FFFF   Shared memory regions (per-process mapped)
0xFE000000 - 0xFEFFFFFF   PCI MMIO (VirtIO GPU BARs, NIC registers)
```

Paging: 4KB pages for first 256MB (PTE_USER for hybrid kernel/user), 4MB PSE pages for 256MB-4GB range.

## License

MIT
