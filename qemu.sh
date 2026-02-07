#!/bin/sh
set -e
. ./iso.sh

# Create disk if doesn't exist (10MB)
if [ ! -f impos_disk.img ]; then
    echo "Creating persistent disk image (10MB)..."
    qemu-img create -f raw impos_disk.img 10M
fi

# Run with disk for persistence
qemu-system-$(./target-triplet-to-arch.sh $HOST) \
    -cdrom myos.iso \
    -drive file=impos_disk.img,format=raw,if=ide \
    -display vnc=:0 \
    -m 128M