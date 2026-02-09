#!/bin/sh
set -e
. ./iso.sh

# Create disk if doesn't exist (10MB)
if [ ! -f impos_disk.img ]; then
    echo "Creating persistent disk image (10MB)..."
    qemu-img create -f raw impos_disk.img 10M
fi

# Run with disk for persistence and RTL8139 network card
qemu-system-$(./target-triplet-to-arch.sh $HOST) \
    -cdrom myos.iso \
    -drive file=impos_disk.img,format=raw,if=ide \
    -netdev user,id=net0 \
    -device rtl8139,netdev=net0 \
    -display vnc=:0 \
    -m 256M \
    -vga std