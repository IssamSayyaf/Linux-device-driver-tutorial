# Makefile for Userspace Test Applications
#
# Build all applications:
#   make
#
# Cross-compile:
#   make CC=arm-linux-gnueabihf-gcc

CC ?= gcc
CFLAGS = -Wall -Wextra -O2
LDFLAGS = -lm

# Target applications
TARGETS = mpu6050_test gps_test

.PHONY: all clean

all: $(TARGETS)

mpu6050_test: mpu6050_test.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

gps_test: gps_test.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(TARGETS)

# Install to /usr/local/bin
install: $(TARGETS)
	install -m 755 $(TARGETS) /usr/local/bin/
