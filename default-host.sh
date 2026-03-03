#!/bin/sh
# Returns the default host triplet based on ARCH env var.
# Defaults to aarch64-linux-gnu (aarch64) if ARCH is not set.
case "${ARCH}" in
    i386)    echo i686-elf ;;
    *)       echo aarch64-linux-gnu ;;
esac
