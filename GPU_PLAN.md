# Mesa GPU Integration Plan for ImposOS

## Goal

Add hardware-accelerated GPU rendering to ImposOS via Mesa, replacing the
current software-only compositor path. Target: OpenGL-accelerated compositing
with VirtIO GPU 3D (virgl) in QEMU first, Intel iris on real hardware later,
and llvmpipe/softpipe as universal fallback.

---

## Current State

**What exists (audited Feb 2026):**

| Subsystem | File | LOC | Status |
|-----------|------|-----|--------|
| VirtIO GPU driver (2D only) | `virtio_gpu.c` | 724 | Production |
| Software graphics engine | `gfx.c` | 1,439 | Production |
| Compositor (layer-based, 64 surfaces) | `compositor.c` | 444 | Production |
| PCI enumeration | `pci.c` | 155 | Production |
| Physical memory manager (bitmap) | `pmm.c` | 136 | Basic |
| Virtual memory manager | `vmm.c` | 211 | Good |
| Syscall interface (15 syscalls) | `syscall.c` | 188 | Minimal |

**What's missing for Mesa:**
- No DRM/KMS kernel subsystem (mode setting, GEM buffer management)
- No ioctl() syscall (DRM is fundamentally ioctl-driven)
- No mmap() syscall (GEM buffer mapping)
- No GPU command submission (VirtIO GPU is 2D-only today)
- No contiguous physical page allocator (PMM is single-frame only)
- No libdrm userspace library
- No LLVM (for shader compilation in Mesa llvmpipe)
- No ELF userspace loader for Mesa (currently kernel-space only for GUI)

---

## Architecture: Staged Approach

Start with minimal kernel infrastructure, then layer Mesa on top incrementally.
Each stage delivers a working system.

### Three Acceleration Paths

```
                     Stage 0 (ioctl/mmap)
                           |
                     Stage 1 (KMS)
                           |
                     Stage 2 (GEM)
                           |
                     Stage 3 (libdrm)
                      /    |    \
                     /     |     \
          Stage 4a      Stage 4b     Stage 5
         (virgl)      (softpipe)     (i915)
      GPU-accel in    Software GL    GPU-accel on
       QEMU/KVM       everywhere    real Intel HW
```

**Fast path:** 0 -> 1 -> 2 -> 3 -> 4a (VirtIO GPU 3D in QEMU)
**Safe path:** 0 -> 1 -> 2 -> 3 -> 4b (software OpenGL, no GPU needed)
**Hard path:** 0 -> 1 -> 2 -> 3 -> 5  (real Intel GPU acceleration)

---

## Stage 0 -- Kernel Syscall Infrastructure (Week 1)

**Goal:** Add ioctl() and mmap() syscalls. Everything else depends on these.

**Why this is Stage 0:** The DRM subsystem is fundamentally ioctl-driven.
Even `DRM_IOCTL_MODE_GETRESOURCES` needs a working ioctl dispatch path. The
original plan deferred this to Stage 3 (libdrm), but it's actually blocking
from Stage 1 onward.

**What to build:**

1. **ioctl() syscall** (~200 LOC)
   - Generic ioctl dispatcher: `int ioctl(int fd, unsigned long cmd, void *arg)`
   - Route by file descriptor type (device node -> driver-specific handler)
   - DRM device: `/dev/dri/card0` -> `drm_ioctl()` dispatcher
   - Add SYS_IOCTL to syscall table
   - Files: extend `syscall.c`, add `kernel/include/kernel/ioctl.h`

2. **mmap() syscall** (~300 LOC)
   - Map physical pages into caller's virtual address space
   - For GEM buffers: map GEM object's physical pages at a chosen VA
   - Minimal implementation: no demand paging, no copy-on-write
   - Just: allocate VA range, insert PTEs, return address
   - Add SYS_MMAP to syscall table
   - Files: extend `syscall.c`, extend `vmm.c`

3. **Device node routing** (~100 LOC)
   - Extend fd table to support device file descriptors
   - `open("/dev/dri/card0")` returns an fd with type=DRM_DEVICE
   - ioctl() on that fd routes to DRM subsystem
   - Files: extend `fs.c` chardev dispatch

**Deliverable:** `ioctl(fd, DRM_IOCTL_VERSION, &ver)` returns a version
struct. `mmap()` maps a physical page and the caller can read/write it.

**Estimated LOC:** ~600

---

## Stage 1 -- KMS Modesetting (Weeks 2-4)

**Goal:** Set GPU display mode from the kernel, output to hardware displays.
Abstract the display pipeline into the standard CRTC/encoder/connector model.

**What to build:**

1. **DRM core subsystem** (~500 LOC)
   - `drm_device_t` structure: CRTCs, encoders, connectors
   - Mode object management (mode list, current mode)
   - ioctl dispatcher: route DRM_IOCTL_* to handler functions
   - Files: `kernel/arch/i386/drivers/drm_core.c`,
            `kernel/include/kernel/drm.h`

2. **VirtIO GPU KMS backend** (~400 LOC)
   - Wrap existing `virtio_gpu.c` display commands in KMS abstractions
   - CRTC: maps to VirtIO scanout
   - Connector: maps to VirtIO display info
   - Mode: read from `VIRTIO_GPU_CMD_GET_DISPLAY_INFO`
   - Scanout: `VIRTIO_GPU_CMD_SET_SCANOUT` via `DRM_IOCTL_MODE_SETCRTC`
   - Files: `kernel/arch/i386/drivers/virtio_gpu_kms.c`

3. **BGA/VBE fallback backend** (~200 LOC)
   - For non-VirtIO QEMU configs (-vga std, -vga vmware)
   - BGA registers: set resolution, enable LFB
   - Single CRTC, single connector, single fixed mode
   - Files: extend existing code in `gfx.c` or new `bga_kms.c`

**KMS ioctls to implement (minimal set):**

| ioctl | Purpose |
|-------|---------|
| `DRM_IOCTL_VERSION` | Driver name + version |
| `DRM_IOCTL_MODE_GETRESOURCES` | List CRTCs, connectors, encoders |
| `DRM_IOCTL_MODE_GETCONNECTOR` | Get connector status + modes |
| `DRM_IOCTL_MODE_GETENCODER` | Get encoder -> CRTC mapping |
| `DRM_IOCTL_MODE_GETCRTC` | Get current CRTC mode |
| `DRM_IOCTL_MODE_SETCRTC` | Set mode + framebuffer |

**Deliverable:** Open `/dev/dri/card0`, query available modes via ioctl,
set a display mode. Existing desktop still works (compositor detects DRM
and uses it for mode setting instead of raw BGA/multiboot framebuffer).

**Estimated LOC:** ~1,100

---

## Stage 2 -- GEM Buffer Management (Weeks 5-7)

**Goal:** GPU-aware memory allocation. This is the foundation Mesa needs to
create textures, renderbuffers, and command buffers.

**Prerequisites:**
- Contiguous page allocator in PMM (multi-frame alloc for scanout buffers)

**What to build:**

1. **PMM contiguous allocator** (~200 LOC)
   - `pmm_alloc_contiguous(n_frames)` -- find N consecutive free frames
   - Needed because a 1920x1080x4 scanout buffer = 2048 contiguous frames
   - Simple first-fit scan of the bitmap
   - Files: extend `pmm.c`

2. **GEM core** (~600 LOC)
   - GEM object: physical pages + size + handle + refcount
   - Handle table (global for now, per-process later)
   - `gem_create()` -- allocate physical pages (contiguous for scanout)
   - `gem_mmap()` -- map GEM object into caller's address space via VMM
   - `gem_close()` -- unref + free when refcount hits 0
   - Files: `kernel/arch/i386/drivers/drm_gem.c`,
            `kernel/include/kernel/drm_gem.h`

3. **Dumb buffer API** (~200 LOC)
   - `DRM_IOCTL_MODE_CREATE_DUMB` -- create scanout-capable buffer
   - `DRM_IOCTL_MODE_MAP_DUMB` -- CPU-map for software rendering
   - `DRM_IOCTL_MODE_DESTROY_DUMB` -- free
   - `DRM_IOCTL_MODE_ADDFB` -- register GEM object as framebuffer
   - This alone enables: alloc buffer -> draw in CPU -> set as scanout

4. **Page flip** (~200 LOC)
   - `DRM_IOCTL_MODE_PAGE_FLIP` -- swap front/back framebuffers
   - VirtIO backend: resource_flush the new buffer
   - Enables tear-free double buffering through DRM

**ioctls to add:**

| ioctl | Purpose |
|-------|---------|
| `DRM_IOCTL_GEM_OPEN` | Open GEM by name |
| `DRM_IOCTL_GEM_CLOSE` | Close GEM handle |
| `DRM_IOCTL_GEM_FLINK` | Share GEM across processes |
| `DRM_IOCTL_MODE_CREATE_DUMB` | Allocate dumb buffer |
| `DRM_IOCTL_MODE_MAP_DUMB` | CPU-map dumb buffer |
| `DRM_IOCTL_MODE_DESTROY_DUMB` | Free dumb buffer |
| `DRM_IOCTL_MODE_ADDFB` | Register as framebuffer |
| `DRM_IOCTL_MODE_PAGE_FLIP` | Swap framebuffers |

**Deliverable:** Compositor allocates framebuffers via DRM dumb buffers
instead of raw `malloc()`. Page flipping works via ioctl. This is already
usable for a tear-free, DRM-backed compositor -- no Mesa needed yet.

**Estimated LOC:** ~1,200

---

## Stage 3 -- libdrm Port (Week 8)

**Goal:** Build libdrm for ImposOS so Mesa has its expected userspace library.

**What to build:**

1. **Minimal libc extensions for DRM** (~300 LOC)
   - Ensure `ioctl()` works for DRM ioctls
   - Ensure `mmap()` works for GEM buffer mapping
   - `open("/dev/dri/card0")` returns valid fd
   - Since compositor runs in kernel, these can be thin wrappers that
     call the kernel DRM functions directly (no context switch needed)

2. **libdrm cross-compilation** (build config, ~0 new code)
   - Cross-compile libdrm with meson, targeting i686-impos
   - Disable all GPU-specific modules initially
   - Key files: `xf86drm.c`, `xf86drmMode.c` (mode setting helpers)
   - Patch: stub `drmIoctl()` to call ImposOS ioctl, remove Linux-isms
   - Static link into kernel (no shared libraries)

3. **Validation tests**
   - `drmModeGetResources()` returns connector/CRTC list
   - `drmModeSetCrtc()` programs the display
   - `drmModeAddFB()` / `drmModePageFlip()` work
   - `drmModeCreateDumbBuffer()` allocates GEM-backed scanout

**Deliverable:** libdrm compiles and links against ImposOS. Mode setting
and dumb buffer allocation work through libdrm's C API.

**Estimated LOC:** ~300 (stubs/glue)

---

## Stage 4a -- VirtIO GPU 3D / virgl (Weeks 9-12)

**Goal:** Hardware-accelerated OpenGL in QEMU via VirtIO GPU 3D and Mesa's
virgl Gallium driver. This is the **fastest path to real GPU acceleration**.

**Why virgl is the best first target:**
- The existing `virtio_gpu.c` already implements virtqueue management, PCI
  probing, and 2D resource commands -- 80% of the infrastructure exists
- Only ~300 LOC of new VirtIO GPU commands needed in the kernel driver
- The host GPU (your real GPU) does all the actual rendering via virglrenderer
- Mesa's virgl driver handles GL state tracking, shader compilation, etc.
- Gives OpenGL 4.3 in QEMU with `-device virtio-vga-gl`
- No GPU register programming, no PLLs, no ring buffers

**What to build:**

1. **VirtIO GPU 3D command support** (~300 LOC)
   - `VIRTIO_GPU_CMD_CTX_CREATE` -- create 3D rendering context
   - `VIRTIO_GPU_CMD_CTX_DESTROY` -- destroy context
   - `VIRTIO_GPU_CMD_CTX_ATTACH_RESOURCE` -- bind resource to context
   - `VIRTIO_GPU_CMD_CTX_DETACH_RESOURCE` -- unbind
   - `VIRTIO_GPU_CMD_RESOURCE_CREATE_3D` -- 3D-capable resource allocation
   - `VIRTIO_GPU_CMD_SUBMIT_3D` -- submit Gallium command stream
   - `VIRTIO_GPU_CMD_TRANSFER_TO_HOST_3D` -- upload texture data
   - `VIRTIO_GPU_CMD_TRANSFER_FROM_HOST_3D` -- readback
   - Files: extend `virtio_gpu.c`, add `virtio_gpu_3d.c`

2. **VirtIO GPU DRM ioctls** (~400 LOC)
   - Map VirtIO GPU 3D commands to DRM ioctl interface
   - `DRM_VIRTGPU_MAP` -- mmap a resource
   - `DRM_VIRTGPU_EXECBUFFER` -- submit 3D command buffer
   - `DRM_VIRTGPU_RESOURCE_CREATE` -- create 3D resource
   - `DRM_VIRTGPU_RESOURCE_INFO` -- query resource details
   - `DRM_VIRTGPU_TRANSFER_FROM_HOST` -- readback
   - `DRM_VIRTGPU_TRANSFER_TO_HOST` -- upload
   - `DRM_VIRTGPU_WAIT` -- wait for GPU completion
   - `DRM_VIRTGPU_GET_CAPS` -- query virgl capabilities
   - Files: `virtio_gpu_drm.c`

3. **Mesa virgl build** (~500 LOC glue)
   - Cross-compile Mesa with: `-Dgallium-drivers=virgl`
   - Disable: EGL platform (or stub), X11, Wayland
   - Static link Mesa into the kernel
   - Stub missing POSIX functions (see table below)
   - Files: `mesa_glue.c` (POSIX stubs + initialization)

4. **OpenGL compositor** (~500 LOC)
   - Initialize Mesa/virgl against DRM device
   - Create an EGL context (or call Mesa internals directly)
   - Render compositor surfaces as textured quads with GL
   - Alpha blending, window shadows via GPU
   - Page flip the result via DRM

**QEMU launch flags:**
```
qemu-system-i386 -device virtio-vga-gl -display sdl,gl=on -m 256M
```

**Deliverable:** Compositor renders windows using OpenGL via VirtIO GPU 3D.
`glClear()` + textured quads work. GPU-accelerated alpha blending. 60fps
compositing in QEMU.

**Estimated LOC:** ~1,700

---

## Stage 4b -- Mesa softpipe (Alternative to 4a)

**Goal:** Software OpenGL via Mesa's softpipe driver. No GPU hardware needed.
Use this as the universal fallback, or as the primary path if virgl proves
problematic.

**Why softpipe, not llvmpipe:**
- llvmpipe requires LLVM (~50-100MB compiled for i686) -- too large for 256MB
- softpipe uses Mesa's TGSI/NIR interpreter -- no JIT, no LLVM dependency
- Performance: ~5-15fps at 1080p for compositing (acceptable for 2D)
- OpenGL 3.0 support (enough for textured quads + alpha blending)

**What to build:**

1. **Mesa softpipe build** (~500 LOC glue)
   - Cross-compile: `-Dgallium-drivers=swrast -Dvulkan-drivers=`
   - Static link into kernel
   - softpipe renders to a CPU buffer
   - Copy buffer to DRM dumb buffer -> page flip

2. **POSIX stubs for Mesa** (~500 LOC)
   - Mesa needs these regardless of softpipe or virgl

| Function | Difficulty | Implementation |
|----------|-----------|----------------|
| `pthread_mutex_*` | Easy | Single-threaded stubs (noop lock/unlock) |
| `pthread_create/join` | Medium | Map to ImposOS task_create/task_wait |
| `pthread_cond_*` | Medium | Stub or implement with scheduler |
| `dlopen/dlsym/dlclose` | Easy | Static link eliminates need; return NULL |
| `sysconf(_SC_PAGE_SIZE)` | Trivial | Return 4096 |
| `getenv` | Trivial | Return from ImposOS env system |
| `clock_gettime` | Easy | Map to PIT ticks + RTC |
| `posix_memalign` | Easy | Aligned malloc wrapper |
| `sched_yield` | Trivial | Map to task_yield() |

**Deliverable:** `glClear(GL_COLOR_BUFFER_BIT)` works. Textured quads render.
Compositor draws windows using OpenGL in software. Slow but correct.

**Estimated LOC:** ~1,000

---

## Stage 5 -- Intel i915 GPU Acceleration (Weeks 13-20)

**Goal:** Real hardware-accelerated OpenGL via Mesa's iris driver on Intel
Gen 7.5+ GPUs (Haswell and later). This is the hardest stage.

**Only pursue this after Stage 4a or 4b is working.** You need a functional
OpenGL compositor to develop and test against.

**What to build:**

1. **Intel GPU PCI detection + MMIO mapping** (~300 LOC)
   - PCI probe for Intel GPU (vendor 0x8086, display class 0x03)
   - Map GPU MMIO BAR (BAR 0) into kernel virtual address space
   - Read GPU generation from PCI device ID
   - Files: `i915_pci.c`

2. **Intel display engine** (~800 LOC)
   - Read EDID from display connector via GMBUS/DDC (I2C over GPU regs)
   - Program display PLL for target pixel clock
   - Configure pipe: timing generator for target resolution
   - Configure plane: point at scanout buffer in GTT
   - KMS integration: CRTC = pipe, connector = port, encoder = DDI/DAC
   - Files: `i915_display.c`
   - Reference: Intel Open Source HD Graphics PRM (publicly available)

3. **Intel GTT (Graphics Translation Table)** (~400 LOC)
   - Read GGTT base + size from MCHBAR
   - Map GEM objects into GPU-visible addresses via GGTT PTEs
   - Insert/remove entries for scanout + render targets
   - Files: `i915_gtt.c`

4. **GPU command submission** (~1,500 LOC)
   - Batch buffer allocation (GEM objects mapped in GGTT)
   - Ring buffer or execlist management (Gen 8+ uses execlists)
   - `I915_GEM_EXECBUFFER2` ioctl -- submit batch to GPU
   - GPU hardware context creation/destruction
   - Fence/sync: know when GPU is done with a buffer
   - Files: `i915_gem.c`, `i915_ring.c`

5. **GPU interrupt handler** (~300 LOC)
   - Register IRQ for Intel GPU (MSI or legacy PCI interrupt)
   - Handle: batch completion, page fault, GPU hang
   - Hang recovery: reset GPU engine, resubmit pending work
   - Files: `i915_irq.c`

6. **Mesa iris enablement** (build config)
   - Switch Mesa: `-Dgallium-drivers=iris`
   - iris talks to kernel via i915 ioctls (see table below)

**i915-specific ioctls to implement (~15-20):**

| ioctl | Purpose |
|-------|---------|
| `I915_GETPARAM` | Query GPU gen, EU count, capabilities |
| `I915_GEM_CREATE` | Allocate GEM buffer object |
| `I915_GEM_MMAP` | CPU-map GEM object |
| `I915_GEM_MMAP_GTT` | CPU-map via GTT (tiled access) |
| `I915_GEM_SET_DOMAIN` | Cache coherency transitions |
| `I915_GEM_PWRITE` | Write data to GEM from CPU |
| `I915_GEM_PREAD` | Read data from GEM to CPU |
| `I915_GEM_EXECBUFFER2` | Submit GPU batch buffer |
| `I915_GEM_WAIT` | Wait for GEM object to be idle |
| `I915_GEM_SET_TILING` | Set buffer tiling mode (X/Y/none) |
| `I915_GEM_GET_TILING` | Query tiling mode |
| `I915_GEM_CONTEXT_CREATE` | Create GPU hardware context |
| `I915_GEM_CONTEXT_DESTROY` | Destroy GPU context |
| `I915_REG_READ` | Read GPU MMIO register (whitelisted) |

**Target hardware:** ThinkPad T440p (Intel HD 4600, Gen 7.5) or ThinkPad
T450s (Intel HD 5500, Gen 8). Both are $50-100, Intel publishes full PRMs.

**Deliverable:** `glxgears` equivalent renders at 60fps on Intel hardware.
Compositor uses GPU-accelerated blending and effects.

**Estimated LOC:** ~3,300

---

## Stage 6 -- AMD radeonsi (Optional, Future)

**Goal:** Extend to AMD GCN/RDNA GPUs. Only pursue with AMD test hardware.

**Key differences from Intel:**
- PCI vendor 0x1002
- Different MMIO register layout
- PM4 command packet format (not Intel batch buffers)
- Separate VRAM + GTT memory domains (vs Intel's unified)
- amdgpu DRM ioctls instead of i915

**Estimated LOC:** ~2,500-3,000 kernel driver

---

## Quick Win: TinyGL (1-2 Days, Any Time)

If the full Mesa path is too ambitious or you need a quick 3D demo, TinyGL
gives immediate OpenGL 1.x in software with zero external dependencies:

1. Download TinyGL source (~4,000 LOC of C)
2. Add to kernel build alongside `gfx.c`
3. Point TinyGL's zbuffer at the existing backbuffer
4. Compositor uses `glBegin/glEnd` for textured quad blitting
5. Supports: textured quads, alpha blending, Z-buffer, basic transforms

**This coexists with the Mesa plan.** Use TinyGL now for demos, replace with
Mesa later. TinyGL compiles in seconds and has no dependencies.

---

## Summary: LOC & Timeline

| Stage | New Kernel LOC | Description | Calendar |
|-------|---------------|-------------|----------|
| 0. ioctl/mmap | ~600 | Syscall infrastructure | 1 week |
| 1. KMS | ~1,100 | Mode setting abstraction | 3 weeks |
| 2. GEM | ~1,200 | Buffer management + PMM upgrade | 3 weeks |
| 3. libdrm | ~300 | Cross-compile + glue | 1 week |
| 4a. virgl | ~1,700 | VirtIO GPU 3D + Mesa virgl | 4 weeks |
| 4b. softpipe | ~1,000 | Software GL (no LLVM) | 2 weeks |
| 5. i915 | ~3,300 | Intel GPU acceleration | 8 weeks |
| 6. radeonsi | ~2,800 | AMD GPU (optional) | 4 weeks |
| **Total (0-4a)** | **~4,900** | **GPU-accelerated GL in QEMU** | **~12 weeks** |
| **Total (0-5)** | **~8,200** | **+ Real Intel HW accel** | **~20 weeks** |

---

## Integration with Roadmap

The GPU work maps onto the existing ROADMAP.md as follows:

| Roadmap Month | GPU Stage | Rationale |
|---------------|-----------|-----------|
| March-May | None | Focus on Linux compat + GUI apps per roadmap |
| June (HW bringup) | **Stage 0 + 1** | ioctl/mmap + KMS fits hardware bringup month |
| July (Kickstarter) | **Stage 2** | GEM during polish; TinyGL for 3D demo |
| August (dev story) | **Stage 3 + 4a** | libdrm + virgl during infrastructure month |
| September+ | **Stage 5** | Intel GPU accel is a post-Kickstarter deep dive |

**Recommendation:** Don't start Mesa work before June. The software compositor
is perfectly adequate for the Kickstarter demo. TinyGL can provide a quick 3D
wow-factor for the demo video if needed.

---

## Hardware Targets

| GPU | Mesa Driver | Kernel Driver | Priority |
|-----|------------|---------------|----------|
| VirtIO GPU 3D (QEMU) | virgl | virtio_gpu (extend) | **Primary** |
| Any CPU (fallback) | softpipe | KMS + dumb buffers | **Primary** |
| Intel HD 4600 (Haswell, Gen 7.5) | iris | i915 | Secondary |
| Intel HD 5500 (Broadwell, Gen 8) | iris | i915 | Secondary |
| AMD Polaris/RDNA | radeonsi | amdgpu | Future |
| NVIDIA (any) | nouveau | -- | **Skip** |

---

## Files to Create

```
kernel/
  include/kernel/
    ioctl.h             # Generic ioctl definitions
    drm.h               # DRM/KMS types, ioctl numbers, CRTC/connector/encoder
    drm_gem.h           # GEM buffer object types + API
    i915.h              # Intel-specific ioctl numbers + types (Stage 5)
  arch/i386/
    drivers/
      drm_core.c        # DRM subsystem core: device, ioctl dispatch, CRTC mgmt
      drm_gem.c         # GEM buffer allocation, handle table, mmap
      virtio_gpu_kms.c  # VirtIO GPU KMS backend (wraps existing driver)
      virtio_gpu_3d.c   # VirtIO GPU 3D commands (Stage 4a)
      virtio_gpu_drm.c  # VirtIO GPU DRM ioctls (Stage 4a)
      bga_kms.c         # Bochs VGA KMS backend (fallback)
      i915_pci.c        # Intel PCI detection + MMIO map (Stage 5)
      i915_display.c    # Intel display engine: PLL, pipe, plane (Stage 5)
      i915_gtt.c        # Intel GTT management (Stage 5)
      i915_gem.c        # Intel GEM + execbuffer (Stage 5)
      i915_ring.c       # Intel GPU ring/execlist submission (Stage 5)
      i915_irq.c        # Intel GPU interrupt handler (Stage 5)
    sys/
      mesa_glue.c       # POSIX stubs for Mesa (pthread, dlopen, etc.)
```

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Mesa build too complex for ImposOS | Blocks Stage 4 | Static link, bypass EGL/GBM, call Mesa internals directly |
| LLVM too large (50-100MB) | OOM on 256MB system | Use softpipe (no LLVM) instead of llvmpipe |
| pthreads dependency in Mesa | Build failure | Single-threaded stubs initially; implement real threads later |
| VirtIO GPU 3D not working in QEMU | Blocks Stage 4a | Fall back to softpipe (Stage 4b) |
| i915 kernel driver too complex | Blocks Stage 5 | virgl (4a) or softpipe (4b) are production-ready fallbacks |
| GPU hangs during development | Machine unresponsive | Always test in QEMU first; have serial console for debugging |
| Everything in kernel space | Mesa expects userspace | Call Mesa as a library; ioctl/mmap become direct kernel calls |
| PMM fragmentation | Can't alloc scanout buffers | Contiguous allocator in Stage 2; reserve pool at boot if needed |
| 32-bit address space limit | Can't map large VRAM | Intel Gen7/8 GGTT is 32-bit anyway; not an issue at 1080p |

---

## References

- [Intel Open Source HD Graphics PRM](https://01.org/linuxgraphics/documentation) -- full GPU register docs
- [Mesa source](https://gitlab.freedesktop.org/mesa/mesa) -- iris in `src/gallium/drivers/iris/`, virgl in `src/gallium/drivers/virgl/`
- [libdrm source](https://gitlab.freedesktop.org/mesa/drm) -- `xf86drm.c`, `xf86drmMode.c`
- [VirtIO GPU spec](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html) -- Section 5.7 (GPU Device)
- [virglrenderer](https://gitlab.freedesktop.org/virgl/virglrenderer) -- host-side renderer for VirtIO GPU 3D
- [SerenityOS GPU work](https://github.com/SerenityOS/serenity/tree/master/Kernel/Devices/GPU) -- similar OSDev project
- [Linux i915 driver](https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/i915) -- reference (~100K LOC)
- [Linux virtio-gpu driver](https://github.com/torvalds/linux/tree/master/drivers/gpu/drm/virtio) -- reference (~5K LOC, much simpler)
- [TinyGL](https://bellard.org/TinyGL/) -- quick OpenGL 1.x alternative
- [OSDev Wiki: VGA Hardware](https://wiki.osdev.org/VGA_Hardware) -- BGA/VBE mode setting
