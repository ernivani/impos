SHELL := /bin/bash
export PATH := $(HOME)/opt/cross/bin:$(PATH)

# Disk configuration
DISK_IMAGE := impos_disk.img
DISK_SIZE := 40M

# Use KVM if available and accessible, otherwise fall back to TCG
KVM_FLAG := $(shell if [ -w /dev/kvm ] 2>/dev/null; then echo "$(KVM_FLAG)"; fi)

.PHONY: all build iso run run-disk run-us clean rebuild clean-disk help download-busybox

all: iso

build:
	./build.sh

# Download static BusyBox i386 (musl) binary
download-busybox:
	@if [ ! -f test_programs/busybox ]; then \
		echo "Downloading BusyBox i386 static binary..."; \
		curl -L -o test_programs/busybox "https://busybox.net/downloads/binaries/1.35.0-i686-linux-musl/busybox"; \
		chmod +x test_programs/busybox; \
	else \
		echo "BusyBox already downloaded."; \
	fi

initrd.tar:
	@echo "Creating initrd.tar..."
	@rm -rf initrd_staging
	@mkdir -p initrd_staging/etc
	@mkdir -p initrd_staging/bin
	@mkdir -p initrd_staging/usr/bin
	@mkdir -p initrd_staging/tmp
	@echo "Welcome to ImposOS!" > initrd_staging/etc/motd
	@if [ -f test_programs/busybox ]; then \
		echo "Including BusyBox in initrd..."; \
		cp test_programs/busybox initrd_staging/bin/busybox; \
		ln -sf busybox initrd_staging/bin/ls; \
		ln -sf busybox initrd_staging/bin/cat; \
		ln -sf busybox initrd_staging/bin/echo; \
		ln -sf busybox initrd_staging/bin/pwd; \
		ln -sf busybox initrd_staging/bin/uname; \
		ln -sf busybox initrd_staging/bin/id; \
		ln -sf busybox initrd_staging/bin/wc; \
		ln -sf busybox initrd_staging/bin/head; \
		ln -sf busybox initrd_staging/bin/tail; \
	fi
	@cd initrd_staging && tar cf ../initrd.tar --format=ustar .
	@rm -rf initrd_staging

iso: build initrd.tar
	mkdir -p isodir/boot/grub
	cp sysroot/boot/myos.kernel isodir/boot/myos.kernel
	@if [ -f doom1.wad ]; then cp doom1.wad isodir/boot/doom1.wad; fi
	cp initrd.tar isodir/boot/initrd.tar
	@printf 'set gfxmode=1024x768x32,auto\nset gfxpayload=keep\nmenuentry "myos" {\n\tmultiboot /boot/myos.kernel\n\tmodule /boot/doom1.wad\n\tmodule /boot/initrd.tar\n}\n' > isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

$(DISK_IMAGE):
	@echo "Creating disk image: $(DISK_IMAGE) ($(DISK_SIZE))"
	qemu-img create -f raw $(DISK_IMAGE) $(DISK_SIZE)

run: iso $(DISK_IMAGE)
	@echo "=== Running ImposOS with persistent disk ==="
	@echo "Boot: myos.iso (CD-ROM)"
	@echo "Disk: $(DISK_IMAGE) (IDE)"
	@echo "HTTP: http://localhost:8080/ (forwarded to guest port 80)"
	@echo ""
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0,hostfwd=tcp::8080-:80 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 4G \
		-vga virtio \
		-display gtk \
		-audiodev pa,id=snd0 \
		-machine pcspk-audiodev=snd0 \
		$(KVM_FLAG)

run-vnc: iso $(DISK_IMAGE)
	@echo "=== Running ImposOS with VNC display ==="
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 4G \
		-vga virtio \
		-display vnc=:0 \
		-k fr \
		$(KVM_FLAG)

run-disk: run
	@# Alias for 'make run'

run-us: iso $(DISK_IMAGE)
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 4G \
		-vga virtio \
		-display gtk \
		-k en-us \
		$(KVM_FLAG)

run-debug: iso $(DISK_IMAGE)
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0,hostfwd=tcp::8080-:80 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 4G \
		-vga virtio \
		-display gtk \
		-serial file:serial.log \
		$(KVM_FLAG)

run-gtk: iso
	qemu-system-i386 -cdrom myos.iso -vga virtio -m 4G -display gtk $(KVM_FLAG)

clean:
	./clean.sh
	rm -f initrd.tar
	rm -rf initrd_staging

clean-disk:
	@echo "Removing disk image: $(DISK_IMAGE)"
	rm -f $(DISK_IMAGE)

rebuild: clean all

help:
	@echo "ImposOS Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all          Build ISO (default)"
	@echo "  build        Build kernel and libraries"
	@echo "  iso          Create bootable ISO"
	@echo "  run          Run with persistent disk (GTK display)"
	@echo "  run-disk     Alias for 'run'"
	@echo "  run-vnc      Run with persistent disk (VNC display)"
	@echo "  run-us       Run with US keyboard layout"
	@echo "  run-gtk      Run with GTK display"
	@echo "  clean        Clean build artifacts"
	@echo "  clean-disk   Remove disk image"
	@echo "  rebuild      Clean and rebuild everything"
	@echo "  help         Show this help message"
	@echo ""
	@echo "Persistent Disk:"
	@echo "  File: $(DISK_IMAGE)"
	@echo "  Size: $(DISK_SIZE)"
	@echo "  Auto-creates on first run"
	@echo "  Remove with 'make clean-disk' to start fresh"
	@echo ""
	@echo "Note: 'make run' now uses persistent disk by default!"
