# Master Makefile for Linux Device Driver Tutorial
#
# This Makefile builds all kernel modules and userspace applications.
#
# Usage:
#   make                    # Build all modules and apps
#   make modules            # Build only kernel modules
#   make apps               # Build only userspace apps
#   make clean              # Clean all build artifacts
#
# Cross-compilation:
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KERNEL_SRC=/path/to/kernel

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Kernel module directories
MODULE_DIRS = \
    drivers/peripheral/uart \
    drivers/peripheral/spi \
    drivers/sensors/mpu6050 \
    drivers/sensors/ultrasonic \
    drivers/gps/neo_m8n

# Userspace application directory
APP_DIR = apps

.PHONY: all modules apps clean help

all: modules apps

modules:
	@echo "Building kernel modules..."
	@for dir in $(MODULE_DIRS); do \
		echo "  Building $$dir"; \
		$(MAKE) -C $(KERNEL_SRC) M=$(PWD)/$$dir modules || exit 1; \
	done
	@echo "Kernel modules built successfully"

apps:
	@echo "Building userspace applications..."
	$(MAKE) -C $(APP_DIR)
	@echo "Applications built successfully"

clean:
	@echo "Cleaning kernel modules..."
	@for dir in $(MODULE_DIRS); do \
		$(MAKE) -C $(KERNEL_SRC) M=$(PWD)/$$dir clean 2>/dev/null || true; \
	done
	@echo "Cleaning applications..."
	$(MAKE) -C $(APP_DIR) clean
	@echo "Clean complete"

help:
	@echo "Linux Device Driver Tutorial - Build System"
	@echo ""
	@echo "Targets:"
	@echo "  all      - Build all modules and apps (default)"
	@echo "  modules  - Build only kernel modules"
	@echo "  apps     - Build only userspace applications"
	@echo "  clean    - Clean all build artifacts"
	@echo "  help     - Show this help message"
	@echo ""
	@echo "Variables:"
	@echo "  KERNEL_SRC     - Path to kernel source (default: running kernel)"
	@echo "  ARCH           - Target architecture (for cross-compile)"
	@echo "  CROSS_COMPILE  - Cross-compiler prefix"
	@echo ""
	@echo "Examples:"
	@echo "  make                              # Build for current system"
	@echo "  make KERNEL_SRC=/usr/src/linux    # Build for specific kernel"
	@echo "  make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-"
	@echo ""
