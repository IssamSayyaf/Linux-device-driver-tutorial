# Linux Device Driver Development Tutorial

## Complete Guide for Embedded Linux Driver Development

This comprehensive tutorial covers everything you need to know about writing Linux device drivers, from basic kernel modules to production-ready drivers for real hardware.

---

## Table of Contents

1. [Roadmap Overview](#roadmap-overview)
2. [Tutorial Structure](#tutorial-structure)
3. [Quick Start](#quick-start)
4. [Subsystem Guide](#subsystem-guide)

---

## Roadmap Overview

```
+------------------------------------------------------------------+
|                 LINUX DRIVER DEVELOPMENT ROADMAP                   |
+------------------------------------------------------------------+
                              |
                              v
        +---------------------------------------------+
        |          PHASE 1: FUNDAMENTALS              |
        |  - Kernel Module Basics                     |
        |  - Character Device Drivers                 |
        |  - Memory Management (kmalloc, vmalloc)     |
        |  - Synchronization (mutex, spinlock, atomic)|
        +---------------------------------------------+
                              |
                              v
        +---------------------------------------------+
        |          PHASE 2: BUS SUBSYSTEMS            |
        |  - I2C Subsystem                            |
        |  - SPI Subsystem                            |
        |  - UART/Serial Subsystem                    |
        |  - Platform Devices                         |
        +---------------------------------------------+
                              |
                              v
        +---------------------------------------------+
        |          PHASE 3: DEVICE SUBSYSTEMS         |
        |  - IIO (Industrial I/O) - Sensors          |
        |  - GPIO Subsystem                           |
        |  - Input Subsystem                          |
        |  - GNSS Subsystem                           |
        |  - Network Subsystem                        |
        +---------------------------------------------+
                              |
                              v
        +---------------------------------------------+
        |          PHASE 4: ADVANCED TOPICS           |
        |  - DMA Engine                               |
        |  - Interrupt Handling (threaded IRQ)        |
        |  - Power Management                         |
        |  - Device Tree                              |
        +---------------------------------------------+
                              |
                              v
        +---------------------------------------------+
        |          PHASE 5: REAL DRIVERS              |
        |  - MPU6050 (Accelerometer/Gyroscope)        |
        |  - uBlox NEO-M8N (GPS)                      |
        |  - Ultrasonic Sensor (HC-SR04)              |
        |  - BMP280 (Pressure/Temperature)            |
        +---------------------------------------------+
```

---

## Tutorial Structure

```
Linux-device-driver-tutorial/
├── docs/                          # Documentation and guides
│   ├── 00-environment-setup.md    # Development environment setup
│   ├── 01-kernel-module-basics.md # Kernel module fundamentals
│   ├── 02-character-devices.md    # Character device drivers
│   ├── 03-memory-management.md    # Kernel memory management
│   ├── 04-synchronization.md      # Locks, mutexes, semaphores
│   ├── 05-i2c-subsystem.md        # I2C bus subsystem
│   ├── 06-spi-subsystem.md        # SPI bus subsystem
│   ├── 07-uart-subsystem.md       # UART/Serial subsystem
│   ├── 08-iio-subsystem.md        # Industrial I/O subsystem
│   ├── 09-gpio-subsystem.md       # GPIO subsystem
│   ├── 10-kconfig-build-system.md # Kconfig and build system
│   │
│   │   # Real Driver Case Studies
│   ├── 20-mpu6050-tutorial.md     # MPU6050 IMU (I2C + IIO + Regmap)
│   ├── 21-hc-sr04-tutorial.md     # HC-SR04 Ultrasonic (GPIO + IRQ)
│   ├── 22-neo-m8n-gps-tutorial.md # NEO-M8N GPS (GNSS + Serdev)
│   ├── 23-uart-driver-tutorial.md # Custom UART (TTY + DMA)
│   └── 24-spi-controller-tutorial.md # SPI Controller (DMA)
│
├── drivers/                       # Driver source code
│   ├── peripheral/                # Bus/Peripheral drivers
│   │   ├── uart/                  # Full UART driver
│   │   ├── spi/                   # Full SPI driver with DMA
│   │   └── i2c/                   # I2C master driver
│   │
│   ├── sensors/                   # Sensor drivers
│   │   ├── mpu6050/               # MPU6050 accelerometer/gyro
│   │   ├── ultrasonic/            # HC-SR04 ultrasonic
│   │   └── bmp280/                # BMP280 pressure sensor
│   │
│   ├── gps/                       # GPS drivers
│   │   └── neo_m8n/               # uBlox NEO-M8N GPS
│   │
│   └── misc/                      # Miscellaneous drivers
│
├── apps/                          # Userspace applications
├── scripts/                       # Build and test scripts
├── include/                       # Shared headers
└── devicetree/                    # Device tree overlays
```

---

## Quick Start

### Prerequisites
- Linux development machine (Ubuntu 20.04+ recommended)
- Kernel headers installed
- Cross-compilation toolchain (for embedded targets)

### Build Your First Module
```bash
cd drivers/peripheral/uart
make
sudo insmod custom_uart.ko
dmesg | tail -20
```

---

## Subsystem Guide

### Which Subsystem Should I Use?

| Device Type | Bus | Subsystem | Example |
|------------|-----|-----------|---------|
| Accelerometer/Gyro | I2C/SPI | IIO | MPU6050, LSM6DS3 |
| Temperature Sensor | I2C/SPI | IIO/hwmon | BMP280, LM75 |
| GPS Module | UART | GNSS/TTY | NEO-M8N, NEO-6M |
| Ultrasonic Sensor | GPIO | IIO | HC-SR04 |
| Display | SPI/I2C | DRM/FB | SSD1306, ILI9341 |
| Touch Controller | I2C | Input | FT5x06 |
| WiFi/BT Module | SDIO/USB | net80211 | ESP32, RTL8723 |
| Motor Controller | PWM/GPIO | - | Custom |

### Subsystem Overview

```
+------------------------------------------------------------------+
|                    LINUX KERNEL SUBSYSTEMS                        |
+------------------------------------------------------------------+
|                                                                   |
|   +------------------+    +------------------+    +-------------+ |
|   |   BUS DRIVERS    |    | DEVICE SUBSYSTEMS|    |  FRAMEWORKS | |
|   +------------------+    +------------------+    +-------------+ |
|   |                  |    |                  |    |             | |
|   | - I2C Core       |    | - IIO            |    | - Regmap    | |
|   | - SPI Core       |    | - Input          |    | - Device    | |
|   | - UART Core      |    | - hwmon          |    |   Model     | |
|   | - Platform Bus   |    | - GPIO           |    | - Clock     | |
|   | - USB Core       |    | - PWM            |    | - Pinctrl   | |
|   | - PCI            |    | - GNSS           |    | - PM        | |
|   |                  |    | - Network        |    |             | |
|   +------------------+    +------------------+    +-------------+ |
|                                                                   |
+------------------------------------------------------------------+
```

---

## Learning Path by Experience Level

### Beginner Path
1. Environment Setup (`docs/00-environment-setup.md`)
2. Kernel Module Basics (`docs/01-kernel-module-basics.md`)
3. Character Devices (`docs/02-character-devices.md`)
4. Simple GPIO Driver (`drivers/misc/simple_gpio/`)

### Intermediate Path
1. I2C Subsystem (`docs/05-i2c-subsystem.md`)
2. SPI Subsystem (`docs/06-spi-subsystem.md`)
3. IIO Subsystem (`docs/08-iio-subsystem.md`)
4. MPU6050 Driver (`drivers/sensors/mpu6050/`)

### Advanced Path
1. DMA Engine (`docs/11-dma-engine.md`)
2. Full UART Driver (`drivers/peripheral/uart/`)
3. Full SPI Driver with DMA (`drivers/peripheral/spi/`)
4. Production Driver Patterns

---

## Real Driver Case Studies

These tutorials provide in-depth analysis of production-quality drivers in this repository. Each tutorial explains the **why** behind every pattern, not just the how.

### Sensor Drivers

| Tutorial | Driver | Key Concepts Learned |
|----------|--------|---------------------|
| [MPU6050 Tutorial](docs/20-mpu6050-tutorial.md) | 6-axis IMU | Regmap API, IIO triggered buffers, Kconfig options, FIFO handling, DMA allocation |
| [HC-SR04 Tutorial](docs/21-hc-sr04-tutorial.md) | Ultrasonic sensor | GPIO descriptor API, both-edge IRQ, ktime precision timing, completion API |
| [NEO-M8N Tutorial](docs/22-neo-m8n-gps-tutorial.md) | GPS receiver | GNSS subsystem, serdev (serial device), binary protocol parsing, state machines |

### Peripheral/Controller Drivers

| Tutorial | Driver | Key Concepts Learned |
|----------|--------|---------------------|
| [UART Tutorial](docs/23-uart-driver-tutorial.md) | Full UART controller | TTY layer, uart_port, DMA circular buffers, interrupt coalescing, flow control |
| [SPI Tutorial](docs/24-spi-controller-tutorial.md) | SPI master controller | spi_controller, transfer_one callback, GPIO chip select, scatter-gather DMA |

### Driver Skeleton Pattern

Every driver in this repository follows an **8-part structure**:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                      UNIVERSAL DRIVER SKELETON                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  PART 1: INCLUDES & DEFINES                                             │
│    #include <linux/module.h>                                            │
│    #define DRIVER_NAME "my_driver"                                      │
│                                                                          │
│  PART 2: PRIVATE DATA STRUCTURE                                         │
│    struct my_driver_data {                                              │
│        struct device *dev;                                              │
│        void __iomem *base;    /* or i2c_client, spi_device */          │
│        /* ... */                                                        │
│    };                                                                   │
│                                                                          │
│  PART 3: LOW-LEVEL I/O                                                  │
│    static u32 my_read(struct my_driver_data *d, u32 reg);              │
│    static void my_write(struct my_driver_data *d, u32 reg, u32 val);   │
│                                                                          │
│  PART 4: HARDWARE OPERATIONS                                            │
│    static int my_hw_init(struct my_driver_data *d);                    │
│    static int my_read_data(struct my_driver_data *d, int *val);        │
│                                                                          │
│  PART 5: SYSFS/SUBSYSTEM INTERFACE                                      │
│    static ssize_t value_show(...);  /* or IIO read_raw, etc */         │
│                                                                          │
│  PART 6: PROBE FUNCTION                                                 │
│    static int my_probe(struct platform_device *pdev) {                 │
│        /* 1. Allocate private data */                                  │
│        /* 2. Get resources (clocks, regs, IRQ) */                      │
│        /* 3. Initialize hardware */                                    │
│        /* 4. Register with subsystem */                                │
│    }                                                                    │
│                                                                          │
│  PART 7: REMOVE FUNCTION                                                │
│    static int my_remove(struct platform_device *pdev) {                │
│        /* Cleanup in reverse order of probe */                         │
│    }                                                                    │
│                                                                          │
│  PART 8: MODULE GLUE                                                    │
│    static const struct of_device_id my_of_match[] = {...};             │
│    module_platform_driver(my_driver);                                   │
│    MODULE_LICENSE("GPL");                                               │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Hardware Covered

| Device | Interface | Subsystem | Difficulty |
|--------|-----------|-----------|------------|
| LED | GPIO | LED/GPIO | Beginner |
| Button | GPIO | Input | Beginner |
| MPU6050 | I2C | IIO | Intermediate |
| BMP280 | I2C/SPI | IIO | Intermediate |
| HC-SR04 | GPIO | IIO | Intermediate |
| NEO-M8N | UART | GNSS | Advanced |
| Custom UART | Memory-mapped | TTY | Advanced |
| Custom SPI | Memory-mapped | SPI | Advanced |

---

## License

This tutorial is released under GPL v2, consistent with Linux kernel licensing.

## Contributing

Contributions are welcome! Please read the contribution guidelines before submitting pull requests.

---

**Next Step:** Start with [Environment Setup](docs/00-environment-setup.md)
