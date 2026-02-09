#!/bin/sh
set -e
. ./build.sh

mkdir -p isodir
mkdir -p isodir/boot
mkdir -p isodir/boot/grub

cp sysroot/boot/myos.kernel isodir/boot/myos.kernel
cat > isodir/boot/grub/grub.cfg << EOF
set gfxmode=1920x1080x32,1280x1024x32,1024x768x32,auto
set gfxpayload=keep
menuentry "myos" {
	multiboot /boot/myos.kernel
}
EOF
grub-mkrescue -o myos.iso isodir