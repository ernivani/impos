SHELL := /bin/bash
export PATH := $(HOME)/opt/cross/bin:$(PATH)

DISK_IMAGE := impos_disk.img
DISK_SIZE  := 280M

# Platform display backend (cocoa on macOS, gtk on Linux)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  QEMU_DISPLAY := cocoa
else
  QEMU_DISPLAY := gtk
  QEMU_AUDIO   := -audiodev pa,id=snd0 -device AC97,audiodev=snd0
endif

# KVM acceleration if available (i386 only — aarch64 uses TCG)
KVM_FLAG := $(shell [ -w /dev/kvm ] 2>/dev/null && echo "-enable-kvm -cpu host")

INITRD_MODS := $(shell [ -f doom1.wad ] && echo "-initrd doom1.wad,initrd.tar" || echo "-initrd initrd.tar")

.PHONY: all run test clean \
        build-i386 run-i386 run-i386-gl terminal-i386 test-i386

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  DEFAULT TARGET: aarch64 (QEMU virt, cortex-a72, 8GB)           ║
# ╚═══════════════════════════════════════════════════════════════════╝

# ── Build (aarch64) ──────────────────────────────────────────────
all: initrd.tar
	@# Clean any stale i386 objects to avoid cross-arch contamination
	@if [ -f kernel/myos.kernel ] && file kernel/myos.kernel | grep -q '32-bit'; then \
		echo ">>> Cleaning stale i386 objects..."; \
		cd libc && $(MAKE) clean > /dev/null 2>&1; \
		cd ../kernel && $(MAKE) clean > /dev/null 2>&1; \
	fi
	./build.sh

# ── Run (aarch64, serial console) ────────────────────────────────
run: all $(DISK_IMAGE)
	qemu-system-aarch64 \
		-machine virt \
		-cpu cortex-a72 \
		-smp 2 \
		-m 8G \
		-kernel kernel/myos.kernel \
		-drive file=$(DISK_IMAGE),format=raw,if=none,id=hd0 \
		-device virtio-blk-device,drive=hd0 \
		-netdev user,id=net0 \
		-device virtio-net-device,netdev=net0 \
		-nographic

# ── Automated Tests (aarch64) ────────────────────────────────────
test: all
	./test_auto.sh --no-build

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  LEGACY: i386 targets (GRUB multiboot, IDE, VGA)                ║
# ╚═══════════════════════════════════════════════════════════════════╝

# ── Build (i386) ─────────────────────────────────────────────────
build-i386: initrd.tar
	@# Clean any stale aarch64 objects to avoid cross-arch contamination
	@if [ -f kernel/myos.kernel ] && file kernel/myos.kernel | grep -q '64-bit'; then \
		echo ">>> Cleaning stale aarch64 objects..."; \
		cd libc && $(MAKE) clean > /dev/null 2>&1; \
		cd ../kernel && $(MAKE) clean > /dev/null 2>&1; \
	fi
	ARCH=i386 ./build.sh

# ── Run (i386, graphical window) ─────────────────────────────────
run-i386: build-i386 $(DISK_IMAGE)
	qemu-system-i386 \
		-kernel kernel/myos.kernel \
		$(INITRD_MODS) \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-device virtio-tablet-pci \
		-m 4G \
		-vga virtio \
		-display $(QEMU_DISPLAY) \
		-serial stdio \
		$(QEMU_AUDIO) \
		$(KVM_FLAG)

# ── Run (i386, virtio-vga-gl 3D) ────────────────────────────────
DISPLAY_GL ?= sdl
run-i386-gl: build-i386 $(DISK_IMAGE)
	qemu-system-i386 \
		-kernel kernel/myos.kernel \
		$(INITRD_MODS) \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-device virtio-tablet-pci \
		-m 4G \
		-device virtio-vga-gl \
		-display $(DISPLAY_GL),gl=on \
		-serial stdio \
		$(QEMU_AUDIO) \
		$(KVM_FLAG)

# ── Terminal (i386, text shell) ──────────────────────────────────
terminal-i386: build-i386 $(DISK_IMAGE)
	qemu-system-i386 \
		-kernel kernel/myos.kernel \
		$(INITRD_MODS) \
		-append terminal \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-m 4G \
		-vga std \
		-display $(QEMU_DISPLAY) \
		-serial stdio \
		$(KVM_FLAG)

# ── Tests (i386) ─────────────────────────────────────────────────
test-i386: build-i386
	ARCH=i386 ./test_auto.sh --no-build

# ╔═══════════════════════════════════════════════════════════════════╗
# ║  Shared targets                                                  ║
# ╚═══════════════════════════════════════════════════════════════════╝

initrd.tar:
	@rm -rf initrd_staging
	@mkdir -p initrd_staging/etc initrd_staging/bin initrd_staging/usr/bin initrd_staging/tmp initrd_staging/wallpapers
	@echo "Welcome to ImposOS!" > initrd_staging/etc/motd
	@if [ -f wallpaper.jpg ] && command -v convert >/dev/null 2>&1; then \
		convert wallpaper.jpg -resize 1920x1080 initrd_staging/wallpapers/default.png; \
	elif [ -f wallpaper.png ]; then \
		cp wallpaper.png initrd_staging/wallpapers/default.png; \
	else \
		cc -o tools/gen_wallpaper tools/gen_wallpaper.c 2>/dev/null && \
		tools/gen_wallpaper initrd_staging/wallpapers/default.bmp && \
		rm -f tools/gen_wallpaper; \
	fi
	@if [ -f test_programs/busybox ]; then \
		cp test_programs/busybox initrd_staging/bin/busybox; \
		for cmd in ls cat echo pwd uname id wc head tail; do \
			ln -sf busybox initrd_staging/bin/$$cmd; \
		done; \
	fi
	@if [ -f test_programs/sleep_test ]; then \
		cp test_programs/sleep_test initrd_staging/bin/sleep_test; \
	fi
	@if [ -f test_programs/exec_test ]; then \
		cp test_programs/exec_test initrd_staging/bin/exec_test; \
	fi
	@if [ -f test_programs/exec_target ]; then \
		cp test_programs/exec_target initrd_staging/bin/exec_target; \
	fi
	@if [ -d test_programs/lib ]; then \
		mkdir -p initrd_staging/lib; \
		cp test_programs/lib/* initrd_staging/lib/; \
	fi
	@if [ -f test_programs/hello_dyn ]; then \
		cp test_programs/hello_dyn initrd_staging/bin/hello_dyn; \
	fi
	@cd initrd_staging && tar cf ../initrd.tar --format=ustar .
	@rm -rf initrd_staging

$(DISK_IMAGE):
	qemu-img create -f raw $(DISK_IMAGE) $(DISK_SIZE)

# ── Clean ──────────────────────────────────────────────────────────
clean:
	./clean.sh
	rm -f initrd.tar
	rm -rf initrd_staging isodir
