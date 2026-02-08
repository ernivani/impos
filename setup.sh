#!/bin/bash

# Exit on any error
set -e

echo "Installing dependencies for OS development..."

# Check if running on a Debian-based system
if [ -f /etc/debian_version ]; then
    # Update package lists
    sudo apt-get update

    # Install essential build tools
    sudo apt-get install -y \
        build-essential \
        bison \
        flex \
        libgmp3-dev \
        libmpc-dev \
        libmpfr-dev \
        texinfo \
        libisl-dev \
        gcc \
        g++ \
        make \
        nasm \
        xorriso \
        grub-pc-bin \
        grub-common \
        qemu-system-x86

# Check if running on a Red Hat-based system
elif [ -f /etc/redhat-release ]; then
    # Install essential build tools
    sudo dnf groupinstall -y "Development Tools"
    sudo dnf install -y \
        bison \
        flex \
        gmp-devel \
        libmpc-devel \
        mpfr-devel \
        texinfo \
        isl-devel \
        gcc \
        gcc-c++ \
        make \
        nasm \
        xorriso \
        grub2-tools-extra \
        qemu-system-x86

else
    echo "Unsupported distribution. Please install the following packages manually:"
    echo "- gcc and g++"
    echo "- make"
    echo "- bison"
    echo "- flex"
    echo "- gmp, mpc, and mpfr development libraries"
    echo "- texinfo"
    echo "- nasm"
    echo "- xorriso"
    echo "- grub2"
    echo "- qemu"
    exit 1
fi

echo "All dependencies have been installed successfully!"
echo "You can now proceed with building the operating system." 