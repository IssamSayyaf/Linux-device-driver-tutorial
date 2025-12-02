# Environment Setup for Linux Driver Development

## Overview

This guide covers setting up a complete development environment for Linux kernel driver development, including both native development and cross-compilation for embedded targets.

---

## Table of Contents

1. [Host System Requirements](#host-system-requirements)
2. [Installing Development Tools](#installing-development-tools)
3. [Kernel Headers Installation](#kernel-headers-installation)
4. [Cross-Compilation Setup](#cross-compilation-setup)
5. [QEMU for Testing](#qemu-for-testing)
6. [Debugging Tools](#debugging-tools)
7. [Editor Configuration](#editor-configuration)

---

## Host System Requirements

### Recommended Setup
- **OS:** Ubuntu 20.04 LTS or newer (Debian-based preferred)
- **RAM:** 8GB minimum, 16GB recommended
- **Storage:** 50GB free space minimum
- **CPU:** Multi-core processor (for parallel compilation)

---

## Installing Development Tools

### Ubuntu/Debian
```bash
# Update package lists
sudo apt update

# Install essential build tools
sudo apt install -y \
    build-essential \
    gcc \
    make \
    bc \
    bison \
    flex \
    libssl-dev \
    libelf-dev \
    libncurses-dev \
    git \
    fakeroot \
    dpkg-dev \
    rsync \
    cpio \
    kmod

# Install additional development tools
sudo apt install -y \
    device-tree-compiler \
    u-boot-tools \
    lzop \
    libfdt-dev \
    swig \
    python3-dev
```

### Fedora/RHEL
```bash
sudo dnf groupinstall "Development Tools"
sudo dnf install \
    kernel-devel \
    kernel-headers \
    ncurses-devel \
    openssl-devel \
    elfutils-libelf-devel \
    bc \
    bison \
    flex
```

### Arch Linux
```bash
sudo pacman -S \
    base-devel \
    linux-headers \
    bc \
    cpio \
    inetutils
```

---

## Kernel Headers Installation

### For Native Development

```bash
# Install headers for your current kernel
sudo apt install linux-headers-$(uname -r)

# Verify installation
ls /lib/modules/$(uname -r)/build
# Should show: Kconfig, Makefile, arch, include, scripts, etc.

# Alternative: Check kernel version
uname -r
# Example output: 5.15.0-generic
```

### Verify Headers Are Accessible
```bash
# Create a test directory
mkdir -p ~/driver_test && cd ~/driver_test

# Create minimal test module
cat > test_module.c << 'EOF'
#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Test");
MODULE_DESCRIPTION("Test Module");

static int __init test_init(void)
{
    pr_info("Test module loaded\n");
    return 0;
}

static void __exit test_exit(void)
{
    pr_info("Test module unloaded\n");
}

module_init(test_init);
module_exit(test_exit);
EOF

# Create Makefile
cat > Makefile << 'EOF'
obj-m += test_module.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
EOF

# Build
make

# If successful, you'll see test_module.ko
ls -la *.ko
```

---

## Cross-Compilation Setup

### For ARM (32-bit) - Raspberry Pi, BeagleBone
```bash
# Install ARM cross-compiler
sudo apt install -y \
    gcc-arm-linux-gnueabihf \
    binutils-arm-linux-gnueabihf

# Set environment variables
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

# Verify installation
arm-linux-gnueabihf-gcc --version
```

### For ARM64 (64-bit) - Raspberry Pi 4, Modern SBCs
```bash
# Install ARM64 cross-compiler
sudo apt install -y \
    gcc-aarch64-linux-gnu \
    binutils-aarch64-linux-gnu

# Set environment variables
export ARCH=arm64
export CROSS_COMPILE=aarch64-linux-gnu-

# Verify installation
aarch64-linux-gnu-gcc --version
```

### Linaro Toolchain (Recommended for Production)
```bash
# Download Linaro toolchain
cd /opt
sudo wget https://releases.linaro.org/components/toolchain/binaries/latest-7/arm-linux-gnueabihf/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf.tar.xz

# Extract
sudo tar -xf gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf.tar.xz

# Add to PATH
echo 'export PATH=$PATH:/opt/gcc-linaro-7.5.0-2019.12-x86_64_arm-linux-gnueabihf/bin' >> ~/.bashrc
source ~/.bashrc
```

### Cross-Compiling Kernel Modules
```bash
# Set kernel source path (adjust for your target kernel)
export KERNEL_SRC=/path/to/target/linux/source

# Build module
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- \
     -C $KERNEL_SRC M=$(pwd) modules
```

---

## Building the Linux Kernel (Optional)

### Download Kernel Source
```bash
# Clone mainline kernel
git clone --depth=1 https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
cd linux

# Or download specific version
wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.15.tar.xz
tar -xf linux-5.15.tar.xz
cd linux-5.15
```

### Configure and Build
```bash
# Copy current config as starting point
cp /boot/config-$(uname -r) .config

# Update config (optional)
make olddefconfig
# or
make menuconfig  # Interactive configuration

# Build kernel (use -j for parallel compilation)
make -j$(nproc)

# Build modules
make modules -j$(nproc)

# Install modules (careful: affects system)
sudo make modules_install
```

### Cross-Compile Kernel for ARM
```bash
export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabihf-

# Use default config for target board
make bcm2835_defconfig  # Raspberry Pi
# or
make omap2plus_defconfig  # BeagleBone

# Build
make -j$(nproc) zImage modules dtbs
```

---

## QEMU for Testing

### Install QEMU
```bash
sudo apt install -y \
    qemu-system-arm \
    qemu-system-aarch64 \
    qemu-utils
```

### Create Test Environment
```bash
# Create working directory
mkdir -p ~/qemu-kernel-test && cd ~/qemu-kernel-test

# Download a minimal rootfs (BusyBox-based)
wget https://busybox.net/downloads/binaries/1.35.0-x86_64-linux-musl/busybox

# Or use buildroot for more complete rootfs
git clone https://github.com/buildroot/buildroot.git
cd buildroot
make qemu_arm_versatile_defconfig
make -j$(nproc)
```

### Test ARM Kernel with QEMU
```bash
# Basic ARM emulation with versatile board
qemu-system-arm \
    -M versatilepb \
    -kernel /path/to/zImage \
    -dtb /path/to/versatile-pb.dtb \
    -append "root=/dev/ram rdinit=/sbin/init console=ttyAMA0" \
    -initrd /path/to/rootfs.cpio.gz \
    -nographic

# ARM64 emulation
qemu-system-aarch64 \
    -M virt \
    -cpu cortex-a57 \
    -kernel /path/to/Image \
    -append "root=/dev/vda console=ttyAMA0" \
    -drive file=rootfs.img,format=raw \
    -nographic
```

### Testing Modules in QEMU
```bash
# 1. Build your module for the QEMU kernel
# 2. Include it in the rootfs
# 3. Boot QEMU and test:
insmod /path/to/your_module.ko
lsmod
dmesg | tail
rmmod your_module
```

---

## Debugging Tools

### Install Debug Utilities
```bash
sudo apt install -y \
    gdb \
    gdb-multiarch \
    strace \
    ltrace \
    valgrind \
    perf \
    trace-cmd \
    kernelshark
```

### Enable Kernel Debug Options
```bash
# In kernel config (make menuconfig):
# Kernel hacking -->
#   [*] Kernel debugging
#   [*] Debug filesystem
#   [*] KGDB: kernel debugger
#   [*] Compile the kernel with debug info
#   [*] Enable dynamic printk() support
```

### Using Dynamic Debug
```bash
# Enable dynamic debug at boot
# Add to kernel command line: dyndbg="+p"

# At runtime:
echo 'module your_module +p' > /sys/kernel/debug/dynamic_debug/control

# View enabled debug messages
cat /sys/kernel/debug/dynamic_debug/control
```

### GDB Remote Debugging with QEMU
```bash
# Start QEMU with GDB server
qemu-system-arm -s -S ...  # -s = gdbserver on :1234, -S = freeze at start

# Connect GDB
gdb-multiarch vmlinux
(gdb) target remote :1234
(gdb) continue
```

---

## Editor Configuration

### VSCode Setup
```bash
# Install VSCode (if not installed)
sudo snap install --classic code

# Install useful extensions
code --install-extension ms-vscode.cpptools
code --install-extension ms-vscode.makefile-tools
code --install-extension mhutchie.git-graph
```

Create `.vscode/c_cpp_properties.json`:
```json
{
    "configurations": [
        {
            "name": "Linux Kernel",
            "includePath": [
                "${workspaceFolder}/**",
                "/lib/modules/${config:kernelVersion}/build/include",
                "/lib/modules/${config:kernelVersion}/build/include/uapi",
                "/lib/modules/${config:kernelVersion}/build/arch/x86/include",
                "/lib/modules/${config:kernelVersion}/build/arch/x86/include/generated"
            ],
            "defines": [
                "__KERNEL__",
                "MODULE"
            ],
            "compilerPath": "/usr/bin/gcc",
            "cStandard": "gnu11",
            "intelliSenseMode": "linux-gcc-x64"
        }
    ],
    "version": 4
}
```

### Vim Setup
```bash
# Install kernel style plugin
# Add to ~/.vimrc:
cat >> ~/.vimrc << 'EOF'
" Linux kernel coding style
set tabstop=8
set shiftwidth=8
set noexpandtab
set colorcolumn=80

" Kernel source tags
set tags+=~/linux/tags

" Auto-detect kernel code
autocmd BufRead,BufNewFile *.c,*.h
    \ if search('MODULE_LICENSE\|LINUX_VERSION_CODE', 'nw') |
    \   setlocal tabstop=8 shiftwidth=8 noexpandtab |
    \ endif
EOF
```

### Generate Tags for Code Navigation
```bash
# In kernel source directory
make ARCH=arm tags
make ARCH=arm cscope

# For your driver project
ctags -R .
```

---

## Environment Variables Script

Create `~/driver-dev-env.sh`:
```bash
#!/bin/bash
# Linux Driver Development Environment Setup

# Native development
export KERNEL_SRC=/lib/modules/$(uname -r)/build

# ARM cross-compilation (uncomment as needed)
#export ARCH=arm
#export CROSS_COMPILE=arm-linux-gnueabihf-
#export KERNEL_SRC=/path/to/arm/kernel/source

# ARM64 cross-compilation
#export ARCH=arm64
#export CROSS_COMPILE=aarch64-linux-gnu-
#export KERNEL_SRC=/path/to/arm64/kernel/source

# Useful aliases
alias kbuild='make -C $KERNEL_SRC M=$(pwd)'
alias kclean='make -C $KERNEL_SRC M=$(pwd) clean'

# Print environment
echo "Kernel Development Environment"
echo "=============================="
echo "ARCH: ${ARCH:-native}"
echo "CROSS_COMPILE: ${CROSS_COMPILE:-none}"
echo "KERNEL_SRC: $KERNEL_SRC"

# Verify kernel source
if [ -f "$KERNEL_SRC/Makefile" ]; then
    echo "Kernel source: OK"
else
    echo "WARNING: Kernel source not found at $KERNEL_SRC"
fi
```

Usage:
```bash
chmod +x ~/driver-dev-env.sh
source ~/driver-dev-env.sh
```

---

## Quick Reference

### Build Commands
```bash
# Build module
make -C /lib/modules/$(uname -r)/build M=$(pwd) modules

# Clean
make -C /lib/modules/$(uname -r)/build M=$(pwd) clean

# Load module
sudo insmod module.ko

# Unload module
sudo rmmod module

# View kernel log
dmesg | tail -50

# List loaded modules
lsmod

# Module info
modinfo module.ko
```

### Troubleshooting

| Problem | Solution |
|---------|----------|
| Missing kernel headers | `sudo apt install linux-headers-$(uname -r)` |
| Version magic mismatch | Build against correct kernel version |
| Unknown symbol | Check module dependencies, use `modprobe` |
| Permission denied | Use `sudo` or add user to appropriate groups |
| Compiler version mismatch | Use same GCC version as kernel build |

---

## Next Steps

Once your environment is set up:
1. Proceed to [Kernel Module Basics](01-kernel-module-basics.md)
2. Create your first "Hello World" module
3. Learn about character device drivers

---

**Previous:** [README](../README.md) | **Next:** [Kernel Module Basics](01-kernel-module-basics.md)
