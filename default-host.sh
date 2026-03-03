#!/bin/sh
# Returns the default host triplet based on ARCH env var.
# Defaults to i686-elf (i386) if ARCH is not set.
case "${ARCH}" in
    aarch64) echo aarch64-linux-gnu ;;
    *)       echo i686-elf ;;
esac
