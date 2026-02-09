SHELL := /bin/bash
export PATH := $(HOME)/opt/cross/bin:$(PATH)

# Disk configuration
DISK_IMAGE := impos_disk.img
DISK_SIZE := 10M

.PHONY: all build iso run run-disk run-us clean rebuild clean-disk help

all: iso

build:
	./build.sh

iso: build
	mkdir -p isodir/boot/grub
	cp sysroot/boot/myos.kernel isodir/boot/myos.kernel
	@printf 'menuentry "myos" {\n\tmultiboot /boot/myos.kernel\n}\n' > isodir/boot/grub/grub.cfg
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
		-m 128M

run-vnc: iso $(DISK_IMAGE)
	@echo "=== Running ImposOS with VNC display ==="
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 128M \
		-display vnc=:0 \
		-k fr

run-disk: run
	@# Alias for 'make run'

run-us: iso $(DISK_IMAGE)
	qemu-system-i386 \
		-cdrom myos.iso \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0,media=disk \
		-netdev user,id=net0 \
		-device rtl8139,netdev=net0 \
		-boot d \
		-m 128M \
		-k en-us

run-gtk: iso
	qemu-system-i386 -cdrom myos.iso

clean:
	./clean.sh

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
