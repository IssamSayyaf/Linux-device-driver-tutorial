# Kconfig Build System for Linux Drivers

This guide explains how to use the Linux kernel Kconfig build system to make your drivers configurable. Kconfig allows you to:

- Build drivers as modules (M), built-in (Y), or disabled (N)
- Enable/disable optional features like DMA, interrupts, and debugging
- Configure parameters like buffer sizes and port counts

## Table of Contents

1. [Understanding Kconfig](#understanding-kconfig)
2. [Kconfig Syntax](#kconfig-syntax)
3. [Tristate vs Bool Options](#tristate-vs-bool-options)
4. [Dependencies and Selects](#dependencies-and-selects)
5. [Using CONFIG_ Macros in Code](#using-config_-macros-in-code)
6. [Driver Examples](#driver-examples)
7. [Best Practices](#best-practices)

---

## Understanding Kconfig

Kconfig is the configuration system used by the Linux kernel. It provides:

- A hierarchical menu system for configuring the kernel
- Dependencies between configuration options
- Type checking for configuration values
- Help text for each option

### Configuration Flow

```
Kconfig files → make menuconfig → .config → include/generated/autoconf.h
```

1. **Kconfig files**: Define available options
2. **make menuconfig**: User interface to select options
3. **.config**: Stores selected configuration
4. **autoconf.h**: Generated C header with `#define CONFIG_*` macros

---

## Kconfig Syntax

### Basic Structure

```kconfig
config DRIVER_NAME
    tristate "Human-readable description"
    depends on DEPENDENCY
    select REQUIRED_FEATURE
    default y
    help
      Multi-line help text that explains what this
      driver does and when to enable it.

      If unsure, say N.
```

### Option Types

| Type      | Values          | C Macro                              |
|-----------|-----------------|--------------------------------------|
| tristate  | y, m, n         | CONFIG_DRIVER_NAME (=y or =m)        |
| bool      | y, n            | CONFIG_DRIVER_NAME (=y only)         |
| int       | integer         | CONFIG_DRIVER_NAME=value             |
| hex       | hex value       | CONFIG_DRIVER_NAME=0xvalue           |
| string    | text            | CONFIG_DRIVER_NAME="value"           |

### Keywords

| Keyword   | Purpose                                          |
|-----------|--------------------------------------------------|
| config    | Define a new configuration option                |
| tristate  | Module/built-in/disabled option                  |
| bool      | Yes/no option                                    |
| int       | Integer value                                    |
| hex       | Hexadecimal value                                |
| string    | String value                                     |
| depends on| Required dependencies                            |
| select    | Automatically enable another option              |
| default   | Default value if not specified                   |
| range     | Valid range for int/hex values                   |
| help      | Help text (indented)                             |
| if/endif  | Conditional block                                |
| menu/endmenu | Create a submenu                              |
| menuconfig | Config option that also creates a menu          |

---

## Tristate vs Bool Options

### Tristate (y/m/n)

Use tristate for the main driver option. This allows:
- **y** (built-in): Driver compiled into the kernel image
- **m** (module): Driver compiled as a loadable module (.ko)
- **n** (disabled): Driver not compiled

```kconfig
config MY_DRIVER
    tristate "My Driver"
    help
      Main driver option. Choose M for module, Y for built-in.
```

### Bool (y/n)

Use bool for feature options within a driver:

```kconfig
config MY_DRIVER_DMA
    bool "DMA support"
    depends on MY_DRIVER
    help
      Enable DMA support. Only available when driver is enabled.
```

### Important: Console Option

Console support requires the driver to be built-in (=y), not as a module:

```kconfig
config SERIAL_MY_UART_CONSOLE
    bool "Console on My UART"
    depends on SERIAL_MY_UART=y  # Note: =y requirement
    select SERIAL_CORE_CONSOLE
    help
      Console support requires built-in driver.
```

---

## Dependencies and Selects

### Dependencies (depends on)

The option only appears if all dependencies are met:

```kconfig
config MY_DRIVER_DMA
    bool "DMA support"
    depends on MY_DRIVER        # Requires driver enabled
    depends on DMADEVICES       # Requires DMA subsystem
    help
      DMA support requires both the driver and DMA devices.
```

### Automatic Selection (select)

Automatically enable required features:

```kconfig
config MY_DRIVER
    tristate "My Driver"
    select REGMAP_I2C          # Auto-enables regmap I2C
    select IIO                 # Auto-enables IIO subsystem
```

### Difference Between depends and select

| `depends on X`              | `select X`                        |
|-----------------------------|------------------------------------|
| Hides option if X disabled  | Forces X to be enabled             |
| User must enable X first    | X enabled automatically            |
| Use for optional features   | Use for required frameworks        |

---

## Using CONFIG_ Macros in Code

### Conditional Includes

```c
/* Only include DMA headers when DMA is enabled */
#ifdef CONFIG_MY_DRIVER_DMA
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#endif
```

### Conditional Struct Members

```c
struct my_driver_data {
    struct device *dev;
    void __iomem *regs;

#ifdef CONFIG_MY_DRIVER_DMA
    struct dma_chan *tx_dma;
    struct dma_chan *rx_dma;
    bool dma_enabled;
#endif

#ifdef CONFIG_MY_DRIVER_DEBUG
    unsigned long debug_count;
#endif
};
```

### Conditional Functions

```c
#ifdef CONFIG_MY_DRIVER_DMA

static int my_driver_init_dma(struct my_driver_data *data)
{
    /* DMA initialization code */
    return 0;
}

static void my_driver_release_dma(struct my_driver_data *data)
{
    /* DMA cleanup code */
}

#else /* !CONFIG_MY_DRIVER_DMA */

static inline int my_driver_init_dma(struct my_driver_data *data)
{
    return 0;  /* No-op when DMA disabled */
}

static inline void my_driver_release_dma(struct my_driver_data *data)
{
}

#endif /* CONFIG_MY_DRIVER_DMA */
```

### Using Kconfig Integer Values

```c
/* Default from Kconfig or fallback */
#ifdef CONFIG_MY_DRIVER_BUFFER_SIZE
#define BUFFER_SIZE CONFIG_MY_DRIVER_BUFFER_SIZE
#else
#define BUFFER_SIZE 4096
#endif
```

### Debug Macros Pattern

```c
#ifdef CONFIG_MY_DRIVER_DEBUG
#define my_dbg(dev, fmt, ...) \
    dev_dbg(dev, "[MY_DRV] " fmt, ##__VA_ARGS__)
#else
#define my_dbg(dev, fmt, ...) do { } while (0)
#endif
```

---

## Driver Examples

### Example 1: MPU6050 Sensor Driver

```kconfig
# drivers/sensors/mpu6050/Kconfig

config MPU6050
    tristate "InvenSense MPU6050 6-Axis Sensor"
    depends on I2C
    select IIO
    select IIO_BUFFER
    select IIO_TRIGGERED_BUFFER
    select REGMAP_I2C
    help
      Driver for MPU6050 accelerometer/gyroscope.

if MPU6050

config MPU6050_IRQ
    bool "Interrupt support"
    depends on MPU6050
    default y
    help
      Enable hardware interrupt support for data ready.

config MPU6050_DMA
    bool "DMA buffer support"
    depends on MPU6050
    depends on DMADEVICES
    help
      Enable DMA for bulk data reads.

config MPU6050_DEBUG
    bool "Debug output"
    depends on MPU6050
    help
      Enable verbose debug logging.

endif # MPU6050
```

### Example 2: UART Controller Driver

```kconfig
# drivers/peripheral/uart/Kconfig

config SERIAL_CUSTOM_UART
    tristate "Custom UART Controller"
    depends on HAS_IOMEM
    select SERIAL_CORE
    help
      Custom UART controller driver.

if SERIAL_CUSTOM_UART

config SERIAL_CUSTOM_UART_CONSOLE
    bool "Console support"
    depends on SERIAL_CUSTOM_UART=y  # Must be built-in for console
    select SERIAL_CORE_CONSOLE
    help
      Enable kernel console on Custom UART.

config SERIAL_CUSTOM_UART_DMA
    bool "DMA support"
    depends on SERIAL_CUSTOM_UART
    depends on DMADEVICES
    select DMA_ENGINE
    help
      Enable DMA for TX/RX.

config SERIAL_CUSTOM_UART_DMA_BUFFER_SIZE
    int "DMA buffer size (bytes)"
    depends on SERIAL_CUSTOM_UART_DMA
    default 4096
    range 256 65536
    help
      Size of DMA buffers.

config SERIAL_CUSTOM_UART_NR_UARTS
    int "Maximum UART ports"
    depends on SERIAL_CUSTOM_UART
    default 4
    range 1 16
    help
      Maximum number of UART ports supported.

config SERIAL_CUSTOM_UART_RS485
    bool "RS-485 support"
    depends on SERIAL_CUSTOM_UART
    help
      Enable RS-485 half-duplex mode.

endif # SERIAL_CUSTOM_UART
```

### Example 3: SPI Controller Driver

```kconfig
# drivers/peripheral/spi/Kconfig

config SPI_CUSTOM
    tristate "Custom SPI Controller"
    depends on HAS_IOMEM
    depends on SPI_MASTER
    help
      Custom SPI master controller driver.

if SPI_CUSTOM

config SPI_CUSTOM_DMA
    bool "DMA support"
    depends on SPI_CUSTOM
    depends on DMADEVICES
    select DMA_ENGINE
    help
      Enable DMA for SPI transfers.

config SPI_CUSTOM_DMA_MIN_BYTES
    int "Minimum bytes for DMA"
    depends on SPI_CUSTOM_DMA
    default 64
    range 16 4096
    help
      Minimum transfer size to use DMA.

config SPI_CUSTOM_MAX_CHIPSELECT
    int "Maximum chip selects"
    depends on SPI_CUSTOM
    default 4
    range 1 16
    help
      Maximum chip select lines supported.

config SPI_CUSTOM_DEBUG
    bool "Debug output"
    depends on SPI_CUSTOM
    help
      Enable verbose debug output.

endif # SPI_CUSTOM
```

---

## Best Practices

### 1. Naming Conventions

- Main driver: `CONFIG_<SUBSYSTEM>_<DRIVER_NAME>`
- Sub-options: `CONFIG_<SUBSYSTEM>_<DRIVER_NAME>_<FEATURE>`

Examples:
```
CONFIG_SERIAL_CUSTOM_UART
CONFIG_SERIAL_CUSTOM_UART_DMA
CONFIG_SPI_CUSTOM
CONFIG_SPI_CUSTOM_DEBUG
```

### 2. Default Values

Provide sensible defaults:

```kconfig
config MY_FEATURE
    bool "Optional feature"
    default y          # Enable by default if commonly used
    # or
    default n          # Disable by default if rarely needed
```

### 3. Use Inline Stubs

When a feature is disabled, provide inline no-op functions:

```c
#ifdef CONFIG_FEATURE
int feature_init(void);
void feature_cleanup(void);
#else
static inline int feature_init(void) { return 0; }
static inline void feature_cleanup(void) { }
#endif
```

This avoids `#ifdef` scattered throughout the code.

### 4. Group Related Options

Use `if/endif` to group feature options:

```kconfig
config MY_DRIVER
    tristate "My Driver"

if MY_DRIVER

config MY_DRIVER_FEATURE_A
    bool "Feature A"

config MY_DRIVER_FEATURE_B
    bool "Feature B"

endif # MY_DRIVER
```

### 5. Write Helpful Help Text

```kconfig
config MY_DRIVER_DMA
    bool "DMA support for My Driver"
    depends on MY_DRIVER
    depends on DMADEVICES
    help
      Say Y here to enable DMA support. This can improve
      performance for large transfers by offloading data
      movement to the DMA controller.

      When enabled, transfers smaller than the FIFO size
      will still use PIO for efficiency.

      If unsure, say Y.
```

### 6. Integration with Kernel Build

For in-tree drivers, reference your Kconfig from the parent:

```kconfig
# drivers/spi/Kconfig
source "drivers/spi/custom/Kconfig"
```

For out-of-tree modules, the Kconfig provides documentation but you'll typically enable features via:

```makefile
# In Makefile
ccflags-y += -DCONFIG_SPI_CUSTOM_DMA
ccflags-y += -DCONFIG_SPI_CUSTOM_DEBUG
```

---

## Summary

The Kconfig system provides a powerful way to configure Linux drivers:

1. **Tristate** for main driver (y/m/n)
2. **Bool** for feature options (y/n)
3. **depends on** for dependencies
4. **select** for auto-enabling required frameworks
5. **#ifdef CONFIG_*** in C code for conditional compilation

This allows:
- Smaller kernel images by disabling unused features
- Optimized drivers by removing unused code paths
- Flexible configuration for different hardware setups
- Clean code with minimal runtime overhead
