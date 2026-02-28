# ImposOS — System Benchmarks & Specifications

Measured from source code and runtime output. All values are compiled constants or
observed behavior from `make run-gl` on QEMU 8.x with KVM, 4GB RAM.

## Codebase

| Metric | Value |
|--------|-------|
| Total source (kernel + libc) | 133,766 LOC |
| Kernel binary size | 4,396,392 bytes (~4.2 MB) |
| GUI subsystem | 13,296 LOC (35 files) |
| GPU pipeline (drivers + compositor) | 3,863 LOC (6 files) |
| Networking stack | 2,898 LOC (11 files) |
| Cryptography | 1,354 LOC (8 files) |
| Binary compat (PE + ELF + Win32 DLLs) | 12,400 LOC |
| Shell | 5,223 LOC (66 commands) |
| DOOM port | 67,732 LOC |

## Memory

| Parameter | Value | Source |
|-----------|-------|--------|
| PMM max frames | 65,536 | `pmm.c` bitmap allocator |
| Frame size | 4 KB | identity-mapped |
| Max physical memory | 256 MB | `PMM_MAX_FRAMES * 4KB` |
| Paging (low) | 4 KB pages, 0–256 MB | PTE_USER, identity-mapped |
| Paging (high) | 4 MB PSE pages, 256 MB–4 GB | large page extension |
| Free at boot (typical) | ~236 MB / 60,553 frames | serial log output |
| Kernel heap | `malloc`/`free` on identity-mapped region | `stdlib.h` |
| Shared memory regions | 16 max | `SHM_MAX_REGIONS` |
| Shared memory per region | 64 KB (16 pages) | `SHM_MAX_SIZE` |
| SHM base address | `0x40000000` | per-process mapped |

## Process / Scheduler

| Parameter | Value | Source |
|-----------|-------|--------|
| Max tasks | 32 | `TASK_MAX` |
| Kernel stack per task | 8 KB | `TASK_STACK_SIZE` |
| User stack | PMM-allocated | ring 3, per-process PD |
| Timer frequency | 120 Hz | PIT divisor 9943 |
| Context switch | TSS-based, `esp0` updated per switch | `idt.c` |
| Scheduler | round-robin, preemptive | `sched.c` |
| File descriptors per task | 64 | `MAX_FDS` |
| Syscalls (native INT 0x80) | 15 | `syscall.c` |
| Signals supported | 6 (SIGINT, SIGTERM, SIGKILL, SIGUSR1/2, SIGPIPE) | `signal.c` |
| Pipe buffer size | 4 KB circular | `PIPE_BUF_SIZE` |

## Filesystem

| Parameter | Value | Source |
|-----------|-------|--------|
| FS version | 2 | `fs.h` superblock |
| Block size | 4 KB | `BLOCK_SIZE` |
| Total blocks | 8,192 (32 MB) | `NUM_BLOCKS` |
| Total inodes | 256 | `NUM_INODES` |
| Direct block pointers | 8 (32 KB) | per-inode |
| Indirect block pointers | 1,024 | single-indirect |
| Max file size | ~4.1 MB | `MAX_FILE_SIZE` |
| Disk sectors per block | 8 | `SECTORS_PER_BLOCK` |
| Disk image size | 40 MB | `DISK_SIZE` in Makefile |
| Sync method | dirty bitmap, sector-granular | `fs.c` |
| Device nodes | 4 (`/dev/null`, `/dev/zero`, `/dev/tty`, `/dev/urandom`) | chardev dispatch |

## Display / GPU

| Parameter | Value | Source |
|-----------|-------|--------|
| Resolution | 1920 x 1080 x 32 bpp | boot.S + GRUB gfxmode |
| Framebuffer size | ~8 MB (1920 * 1080 * 4) | BGRA scanout |
| GPU command buffer | 4,096 DWORDs (16 KB) | `CMD_BUF_DWORDS` |
| Max GPU surfaces | 64 | `MAX_GPU_SURFACES` |
| Render target | 1920 x 1080 BGRA texture | virgl resource |
| Vertex buffer | 6 KB (64 quads * 6 verts * 16 bytes) | PMM-backed |
| Shader format | TGSI text (parsed by virgl host) | VS + FS |
| Blend mode | src_alpha / inv_src_alpha | per-surface alpha |
| Presentation | readback + 2D scanout (Y-flip) | TRANSFER_FROM_HOST_3D |
| Fallback chain | virgl 3D -> VirtIO 2D -> Bochs VGA | `compositor.c` |
| Self-test | clear to blue + readback verify | init-time |

## Window Manager

| Parameter | Value | Source |
|-----------|-------|--------|
| Max windows | 32 | `WM_MAX_WINDOWS` |
| Titlebar height | 38 px | `wm2.c` |
| Min window size | 120 x 60 px | resize constraints |
| Max widgets per window | 48 | `UI_MAX_WIDGETS` |
| Widget types | 13 | `ui_widget.h` enum |
| Z-ordering | layer-based, back-to-front | compositor |
| Window operations | drag, resize (edge), minimize, maximize, close | `wm2.c` |
| Theme | GNOME-dark inspired | `ui_theme.c` |

## Networking

| Parameter | Value | Source |
|-----------|-------|--------|
| NIC drivers | RTL8139, PCnet-FAST III | auto-detected |
| Max TCP connections | 8 | `TCP_MAX_CONNECTIONS` |
| TCP buffer size | 4 KB | `TCP_BUFFER_SIZE` |
| TCP MSS | 1,400 bytes | `TCP_MSS` |
| TCP max retries | 5 | `TCP_MAX_RETRIES` |
| Max sockets | 16 | `MAX_SOCKETS` |
| Firewall rules | 16 max, first-match-wins | `FW_MAX_RULES` |
| TLS version | 1.2 | `tls.c` |
| TLS cipher suites | 2 (AES-128-CBC-SHA256) | `tls.c` |
| DHCP | client, auto-assign on boot | `dhcp.c` |
| DNS | recursive resolver | `dns.c` |

## DRM/KMS

| Parameter | Value | Source |
|-----------|-------|--------|
| Max display modes | 8 | `DRM_MAX_MODES` |
| Backend model | CRTC / connector / encoder | `drm_core.c` |
| Buffer management | GEM (dumb buffers) | `drm_core.c` |
| Page flip | synchronous | `drm_core.c` |
| Device node | `/dev/dri/card0` | chardev |

## Cryptography

| Algorithm | Implementation | Source |
|-----------|---------------|--------|
| AES-128 | CBC mode, 16-byte blocks | `aes.c` (215 LOC) |
| SHA-256 | Full NIST implementation | `sha256.c` (131 LOC) |
| HMAC-SHA256 | RFC 2104 | `hmac.c` (88 LOC) |
| RSA 2048-bit | PKCS#1 v1.5, bignum arithmetic | `rsa.c` + `bignum.c` (241 LOC) |
| Elliptic curve | Point add, scalar multiply | `ec.c` (419 LOC) |
| X.509 | ASN.1 DER parsing | `asn1.c` (175 LOC) |
| CSPRNG | PIT + RTC + RDTSC seeded | `prng.c` (85 LOC) |

## Binary Compatibility

| Feature | Coverage | Source |
|---------|----------|--------|
| Win32 PE loader | MZ/PE header, section mapping, import resolution | `pe_loader.c` |
| Win32 DLL shims | 11 DLLs (kernel32, user32, gdi32, msvcrt, advapi32, ws2_32, ole32, shell32, bcrypt, crypt32, gdiplus) | `win32_*.c` |
| Win32 SEH | Structured Exception Handling | `pe_loader.c` |
| Linux ELF loader | Static ELF32, PT_LOAD segments | `elf_loader.c` |
| Linux syscalls | ~20 (exit, read, write, open, mmap2, brk, stat64, etc.) | `linux_syscall.c` (902 LOC) |

## Boot Sequence (measured from serial log)

| Stage | Output |
|-------|--------|
| VirtIO GPU probe | PCI BAR detection, MMIO config, feature negotiation (VIRGL + EDID) |
| Display init | Scanout 1920x1080 resource created |
| PMM init | 60,553 free frames (236 MB free) |
| VMM init | 4KB pages 0–256MB, 4MB PSE 256MB–4GB, CR3 set |
| Initrd | ~1 MB TAR loaded from multiboot module |
| ACPI | PM1a control port + SLP_TYP discovered |
| VirtIO tablet | PCI probe, modern MMIO, queue depth 64 |
| DRM | VirtIO GPU backend with virgl 3D, Stage 2 (GEM) |
| Filesystem | v2, 256 inodes, 8192 blocks |
| Network | RTL8139, MAC assigned |
| GPU compositor | virgl ctx 2, self-test clear=ff0000ff (blue), active |
| Desktop | 3 initial quads (background, splash, taskbar) |
