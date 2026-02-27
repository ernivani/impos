SHELL := /bin/bash
export PATH := $(HOME)/opt/cross/bin:$(PATH)

DISK_IMAGE := impos_disk.img
DISK_SIZE  := 40M

# Platform display backend (cocoa on macOS, gtk on Linux)
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  QEMU_DISPLAY := cocoa
else
  QEMU_DISPLAY := gtk
  QEMU_AUDIO   := -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0
endif

# KVM acceleration if available
KVM_FLAG := $(shell [ -w /dev/kvm ] 2>/dev/null && echo "-enable-kvm -cpu host")

INITRD_MODS := $(shell [ -f doom1.wad ] && echo "-initrd doom1.wad,initrd.tar" || echo "-initrd initrd.tar")

.PHONY: all run terminal clean

# ── Build ──────────────────────────────────────────────────────────
all: initrd.tar
	./build.sh

initrd.tar:
	@rm -rf initrd_staging
	@mkdir -p initrd_staging/etc initrd_staging/bin initrd_staging/usr/bin initrd_staging/tmp
	@echo "Welcome to ImposOS!" > initrd_staging/etc/motd
	@if [ -f test_programs/busybox ]; then \
		cp test_programs/busybox initrd_staging/bin/busybox; \
		for cmd in ls cat echo pwd uname id wc head tail; do \
			ln -sf busybox initrd_staging/bin/$$cmd; \
		done; \
	fi
	@cd initrd_staging && tar cf ../initrd.tar --format=ustar .
	@rm -rf initrd_staging

$(DISK_IMAGE):
	qemu-img create -f raw $(DISK_IMAGE) $(DISK_SIZE)

# ── Run (graphical window) ─────────────────────────────────────────
run: all $(DISK_IMAGE)
	qemu-system-i386 \
		-kernel kernel/myos.kernel \
		$(INITRD_MODS) \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-device virtio-tablet-pci \
		-m 4G \
		-vga virtio \
		-display $(QEMU_DISPLAY),gl=on \
		-serial stdio \
		$(QEMU_AUDIO) \
		$(KVM_FLAG)

# ── Terminal (QEMU window showing text shell instead of desktop) ───
terminal: all $(DISK_IMAGE)
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

# ── Clean ──────────────────────────────────────────────────────────
clean:
	./clean.sh
	rm -f initrd.tar
	rm -rf initrd_staging isodir
