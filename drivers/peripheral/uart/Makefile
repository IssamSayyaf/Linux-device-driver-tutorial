# Makefile for Custom UART Driver
#
# Build out-of-tree:
#   make
#
# Build for specific kernel:
#   make KERNEL_SRC=/path/to/kernel/source
#
# Cross-compile:
#   make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- KERNEL_SRC=/path/to/kernel

obj-m += custom_uart.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

# Extra compiler flags
ccflags-y := -DDEBUG

all: modules

modules:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

install:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules_install

# Development helpers
load:
	sudo insmod custom_uart.ko

unload:
	sudo rmmod custom_uart

reload: unload load

info:
	modinfo custom_uart.ko

dmesg:
	dmesg | tail -30

.PHONY: all modules clean install load unload reload info dmesg
