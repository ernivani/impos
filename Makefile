CC = i686-elf-gcc
AS = i686-elf-as
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -ffreestanding -O2 -nostdlib

OBJS = src/boot.o src/kernel.o

.PHONY: all clean compile verify build run

all: compile verify build

%.o: %.s
	$(AS) $< -o $@

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

compile: $(OBJS)
	$(CC) -T src/linker.ld -o src/myos.bin $(LDFLAGS) $(OBJS) -lgcc

verify: src/myos.bin
	grub-file --is-x86-multiboot src/myos.bin

build: src/myos.bin src/grub.cfg
	mkdir -p isodir/boot/grub
	cp src/myos.bin isodir/boot/myos.bin
	cp src/grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

run: myos.iso
	qemu-system-i386 -cdrom myos.iso

clean:
	rm -f src/*.o src/myos.bin
	rm -f myos.iso
	rm -rf isodir 