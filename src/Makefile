CC = i686-elf-gcc
AS = i686-elf-as
CFLAGS = -std=gnu99 -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -ffreestanding -O2 -nostdlib

OBJS = boot.o kernel.o

.PHONY: all clean compile verify build run

all: compile verify build

%.o: %.s
	$(AS) $< -o $@

%.o: %.c
	$(CC) -c $< -o $@ $(CFLAGS)

compile: $(OBJS)
	$(CC) -T linker.ld -o myos.bin $(LDFLAGS) $(OBJS) -lgcc

verify: myos.bin
	grub-file --is-x86-multiboot myos.bin

build: myos.bin grub.cfg
	mkdir -p isodir/boot/grub
	cp myos.bin isodir/boot/myos.bin
	cp grub.cfg isodir/boot/grub/grub.cfg
	grub-mkrescue -o myos.iso isodir

run: myos.iso
	qemu-system-i386 -cdrom myos.iso

clean:
	rm -f *.o myos.bin myos.iso
	rm -rf isodir
