SHELL := /bin/bash
export PATH := $(HOME)/opt/cross/bin:$(PATH)

.PHONY: all build iso run clean rebuild

all: iso

build:
	./build.sh

iso: build
	mkdir -p isodir/boot/grub
	cp sysroot/boot/myos.kernel isodir/boot/myos.kernel
	@printf 'menuentry "myos" {\n\tmultiboot /boot/myos.kernel\n}\n' > isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

run: iso
	qemu-system-i386 -cdrom myos.iso -display vnc=:0 -k fr 

run-gtk: iso
	qemu-system-i386 -cdrom myos.iso

clean:
	./clean.sh

rebuild: clean all
