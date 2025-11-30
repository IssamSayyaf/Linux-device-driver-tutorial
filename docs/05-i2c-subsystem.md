# I2C Subsystem

## Overview

The I2C (Inter-Integrated Circuit) subsystem provides a framework for communicating with I2C devices. This chapter covers writing I2C client drivers for sensors, EEPROMs, and other I2C peripherals.

---

## Table of Contents

1. [I2C Architecture](#i2c-architecture)
2. [I2C Client Driver Structure](#i2c-client-driver-structure)
3. [Device Registration](#device-registration)
4. [Data Transfer](#data-transfer)
5. [Regmap Integration](#regmap-integration)
6. [Device Tree Binding](#device-tree-binding)
7. [Complete Example](#complete-example)

---

## I2C Architecture

```
+------------------------------------------------------------------+
|                      I2C SUBSYSTEM ARCHITECTURE                   |
+------------------------------------------------------------------+
|                                                                   |
|   Userspace                                                       |
|   +------------------+                                            |
|   | /dev/i2c-X       |  (i2c-dev interface)                       |
|   +------------------+                                            |
|            │                                                      |
|   ─────────┼──────────────────────────────────────────────────    |
|            │                                                      |
|   Kernel   ▼                                                      |
|   +------------------+     +------------------+                    |
|   | I2C Core         |────▶| I2C Client Driver|                   |
|   | (drivers/i2c/)   |     | (Your Driver)    |                   |
|   +------------------+     +------------------+                    |
|            │                        │                             |
|            │                        │                             |
|            ▼                        ▼                             |
|   +------------------+     +------------------+                    |
|   | I2C Adapter      |     | Specific Device  |                   |
|   | (Bus Driver)     |     | (e.g., sensor)   |                   |
|   +------------------+     +------------------+                    |
|            │                                                      |
|            ▼                                                      |
|   +------------------+                                            |
|   | Hardware I2C     |                                            |
|   | Controller       |                                            |
|   +------------------+                                            |
|                                                                   |
+------------------------------------------------------------------+
```

### Key Concepts
- **I2C Adapter:** Hardware controller driver (master)
- **I2C Client:** Device driver (your sensor driver)
- **I2C Core:** Framework connecting adapters and clients

---

## I2C Client Driver Structure

### Essential Headers
```c
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
```

### Driver Structure
```c
/*
 * struct i2c_driver - I2C client driver structure
 */
static struct i2c_driver my_i2c_driver = {
    .driver = {
        .name = "my_sensor",
        .of_match_table = my_of_match,  /* Device tree matching */
        .pm = &my_pm_ops,               /* Power management */
    },
    .probe = my_probe,          /* Called when device detected */
    .remove = my_remove,        /* Called when device removed */
    .id_table = my_id_table,    /* Legacy ID matching */
};

/* Register driver with I2C core */
module_i2c_driver(my_i2c_driver);
```

### Probe and Remove Functions
```c
/*
 * Probe - Called when device is detected
 *
 * @client: I2C client structure
 *
 * Initialize device and register with subsystems
 */
static int my_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct my_device *my_dev;
    int ret;

    /* Allocate device structure */
    my_dev = devm_kzalloc(dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev)
        return -ENOMEM;

    my_dev->client = client;
    i2c_set_clientdata(client, my_dev);

    /* Initialize hardware */
    ret = my_device_init(my_dev);
    if (ret) {
        dev_err(dev, "Failed to initialize device\n");
        return ret;
    }

    dev_info(dev, "Device probed successfully\n");
    return 0;
}

/*
 * Remove - Called when device is removed
 */
static void my_remove(struct i2c_client *client)
{
    struct my_device *my_dev = i2c_get_clientdata(client);

    /* Cleanup */
    my_device_shutdown(my_dev);

    dev_info(&client->dev, "Device removed\n");
}
```

---

## Device Registration

### Device Tree Matching (Modern Method)
```c
/* Compatible strings for device tree matching */
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-sensor" },
    { .compatible = "vendor,my-sensor-v2", .data = &variant_v2 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, my_of_match);
```

### ACPI Matching
```c
#ifdef CONFIG_ACPI
static const struct acpi_device_id my_acpi_ids[] = {
    { "MYSEN001", 0 },
    { }
};
MODULE_DEVICE_TABLE(acpi, my_acpi_ids);
#endif

static struct i2c_driver my_driver = {
    .driver = {
        .name = "my_sensor",
        .of_match_table = my_of_match,
        .acpi_match_table = ACPI_PTR(my_acpi_ids),
    },
    /* ... */
};
```

### Legacy ID Table
```c
/* For non-device-tree systems */
static const struct i2c_device_id my_id_table[] = {
    { "my_sensor", 0 },
    { "my_sensor_v2", 1 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, my_id_table);
```

---

## Data Transfer

### Basic Read/Write Functions
```c
/*
 * i2c_smbus_* functions - Use standard SMBus protocol
 * More portable across different I2C controllers
 */

/* Read single byte from register */
s32 i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command);

/* Write single byte to register */
s32 i2c_smbus_write_byte_data(const struct i2c_client *client,
                              u8 command, u8 value);

/* Read word (2 bytes) from register */
s32 i2c_smbus_read_word_data(const struct i2c_client *client, u8 command);

/* Write word to register */
s32 i2c_smbus_write_word_data(const struct i2c_client *client,
                              u8 command, u16 value);

/* Read block of data */
s32 i2c_smbus_read_i2c_block_data(const struct i2c_client *client,
                                   u8 command, u8 length, u8 *values);

/* Write block of data */
s32 i2c_smbus_write_i2c_block_data(const struct i2c_client *client,
                                    u8 command, u8 length,
                                    const u8 *values);
```

### Example: Basic I2C Operations
```c
static int my_read_register(struct i2c_client *client, u8 reg)
{
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read reg 0x%02x: %d\n",
                reg, ret);
        return ret;
    }

    return ret;
}

static int my_write_register(struct i2c_client *client, u8 reg, u8 val)
{
    int ret;

    ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to write reg 0x%02x: %d\n",
                reg, ret);
        return ret;
    }

    return 0;
}

/* Read 16-bit value (big-endian device) */
static int my_read_word_be(struct i2c_client *client, u8 reg, u16 *val)
{
    int ret;

    ret = i2c_smbus_read_word_data(client, reg);
    if (ret < 0)
        return ret;

    /* SMBus returns little-endian, swap if device is big-endian */
    *val = swab16(ret);
    return 0;
}
```

### Raw I2C Transfers
```c
#include <linux/i2c.h>

/*
 * For non-SMBus devices or complex transactions
 * Use i2c_transfer() with struct i2c_msg
 */

/* Combined write-then-read transaction */
static int my_read_registers(struct i2c_client *client,
                             u8 start_reg, u8 *buf, int len)
{
    struct i2c_msg msgs[2];
    int ret;

    /* Write register address */
    msgs[0].addr = client->addr;
    msgs[0].flags = 0;  /* Write */
    msgs[0].len = 1;
    msgs[0].buf = &start_reg;

    /* Read data */
    msgs[1].addr = client->addr;
    msgs[1].flags = I2C_M_RD;  /* Read */
    msgs[1].len = len;
    msgs[1].buf = buf;

    ret = i2c_transfer(client->adapter, msgs, 2);
    if (ret != 2) {
        dev_err(&client->dev, "I2C transfer failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}

/* Write multiple bytes */
static int my_write_registers(struct i2c_client *client,
                              u8 start_reg, const u8 *data, int len)
{
    u8 *buf;
    int ret;

    buf = kmalloc(len + 1, GFP_KERNEL);
    if (!buf)
        return -ENOMEM;

    buf[0] = start_reg;
    memcpy(&buf[1], data, len);

    ret = i2c_master_send(client, buf, len + 1);
    kfree(buf);

    if (ret != len + 1) {
        dev_err(&client->dev, "I2C write failed: %d\n", ret);
        return ret < 0 ? ret : -EIO;
    }

    return 0;
}
```

---

## Regmap Integration

### Why Use Regmap?
```
+----------------------------------------+
| REGMAP BENEFITS                        |
+----------------------------------------+
| - Unified register access API          |
| - Automatic caching                    |
| - Endianness handling                  |
| - Range checking                       |
| - Debugfs integration                  |
| - IRQ support                          |
+----------------------------------------+
```

### Regmap Configuration
```c
#include <linux/regmap.h>

static const struct regmap_config my_regmap_config = {
    .reg_bits = 8,              /* Register address width */
    .val_bits = 8,              /* Register value width */
    .max_register = 0x7F,       /* Highest register address */

    /* Optional settings */
    .cache_type = REGCACHE_RBTREE,  /* Enable caching */

    /* Volatile registers (not cached) */
    .volatile_reg = my_volatile_reg,

    /* Write-only registers */
    .writeable_reg = my_writeable_reg,

    /* Readable registers */
    .readable_reg = my_readable_reg,
};

/* Check if register is volatile (changes without write) */
static bool my_volatile_reg(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case REG_STATUS:
    case REG_DATA_X:
    case REG_DATA_Y:
    case REG_DATA_Z:
        return true;
    default:
        return false;
    }
}
```

### Using Regmap in Probe
```c
struct my_device {
    struct i2c_client *client;
    struct regmap *regmap;
    /* ... */
};

static int my_probe(struct i2c_client *client)
{
    struct my_device *dev;
    int ret;

    dev = devm_kzalloc(&client->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    /* Initialize regmap */
    dev->regmap = devm_regmap_init_i2c(client, &my_regmap_config);
    if (IS_ERR(dev->regmap)) {
        dev_err(&client->dev, "Failed to init regmap: %ld\n",
                PTR_ERR(dev->regmap));
        return PTR_ERR(dev->regmap);
    }

    /* Verify device ID */
    ret = regmap_read(dev->regmap, REG_WHO_AM_I, &val);
    if (ret) {
        dev_err(&client->dev, "Failed to read WHO_AM_I\n");
        return ret;
    }

    if (val != EXPECTED_ID) {
        dev_err(&client->dev, "Unknown device ID: 0x%02x\n", val);
        return -ENODEV;
    }

    i2c_set_clientdata(client, dev);
    return 0;
}
```

### Regmap Operations
```c
/* Read single register */
int regmap_read(struct regmap *map, unsigned int reg, unsigned int *val);

/* Write single register */
int regmap_write(struct regmap *map, unsigned int reg, unsigned int val);

/* Update specific bits (read-modify-write) */
int regmap_update_bits(struct regmap *map, unsigned int reg,
                       unsigned int mask, unsigned int val);

/* Bulk read */
int regmap_bulk_read(struct regmap *map, unsigned int reg,
                     void *val, size_t val_count);

/* Bulk write */
int regmap_bulk_write(struct regmap *map, unsigned int reg,
                      const void *val, size_t val_count);

/* Set bits */
int regmap_set_bits(struct regmap *map, unsigned int reg, unsigned int bits);

/* Clear bits */
int regmap_clear_bits(struct regmap *map, unsigned int reg, unsigned int bits);
```

### Example: Using Regmap
```c
/* Configure device */
static int my_configure(struct my_device *dev)
{
    int ret;

    /* Write configuration */
    ret = regmap_write(dev->regmap, REG_CTRL1, CTRL1_ENABLE);
    if (ret)
        return ret;

    /* Update specific bits without affecting others */
    ret = regmap_update_bits(dev->regmap, REG_CTRL2,
                             CTRL2_RATE_MASK,
                             CTRL2_RATE_100HZ);
    if (ret)
        return ret;

    return 0;
}

/* Read sensor data */
static int my_read_data(struct my_device *dev, s16 *x, s16 *y, s16 *z)
{
    u8 data[6];
    int ret;

    ret = regmap_bulk_read(dev->regmap, REG_DATA_X_L, data, 6);
    if (ret)
        return ret;

    *x = (s16)((data[1] << 8) | data[0]);
    *y = (s16)((data[3] << 8) | data[2]);
    *z = (s16)((data[5] << 8) | data[4]);

    return 0;
}
```

---

## Device Tree Binding

### Device Tree Entry
```dts
/* Example: sensor on i2c bus */
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;  /* 400 kHz */

    my_sensor@68 {
        compatible = "vendor,my-sensor";
        reg = <0x68>;            /* I2C address */

        /* Optional properties */
        interrupt-parent = <&gpio1>;
        interrupts = <5 IRQ_TYPE_EDGE_FALLING>;

        /* Device-specific properties */
        vdd-supply = <&reg_3v3>;
        sample-rate-hz = <100>;
    };
};
```

### Reading Device Tree Properties
```c
#include <linux/of.h>

static int my_parse_dt(struct my_device *dev, struct device_node *np)
{
    u32 val;
    int ret;

    /* Read optional u32 property */
    ret = of_property_read_u32(np, "sample-rate-hz", &val);
    if (ret == 0)
        dev->sample_rate = val;
    else
        dev->sample_rate = 50;  /* Default */

    /* Read optional string property */
    of_property_read_string(np, "label", &dev->label);

    /* Check if property exists */
    dev->active_low = of_property_read_bool(np, "active-low");

    /* Get regulator */
    dev->vdd = devm_regulator_get(&dev->client->dev, "vdd");
    if (IS_ERR(dev->vdd))
        return PTR_ERR(dev->vdd);

    /* Get GPIO */
    dev->gpio_reset = devm_gpiod_get_optional(&dev->client->dev,
                                               "reset", GPIOD_OUT_HIGH);

    return 0;
}
```

---

## Complete Example

### Full I2C Temperature Sensor Driver

```c
/*
 * my_temp_sensor.c - I2C Temperature Sensor Driver
 *
 * Example driver for a hypothetical temperature sensor
 * demonstrating I2C and IIO subsystem integration.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define DRIVER_NAME "my_temp_sensor"

/* Register definitions */
#define REG_WHO_AM_I        0x00
#define REG_CTRL            0x01
#define REG_STATUS          0x02
#define REG_TEMP_L          0x03
#define REG_TEMP_H          0x04

#define WHO_AM_I_VALUE      0x5A
#define CTRL_ENABLE         BIT(0)
#define STATUS_READY        BIT(0)

struct my_temp_device {
    struct i2c_client *client;
    struct regmap *regmap;
};

/* Regmap configuration */
static const struct regmap_config my_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = REG_TEMP_H,
};

/* IIO channel definition */
static const struct iio_chan_spec my_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
    },
};

/* Read temperature */
static int my_read_temp(struct my_temp_device *dev, int *temp)
{
    u8 data[2];
    int ret;

    ret = regmap_bulk_read(dev->regmap, REG_TEMP_L, data, 2);
    if (ret)
        return ret;

    /* Convert to temperature (example: 0.1°C per LSB) */
    *temp = (s16)((data[1] << 8) | data[0]);
    return 0;
}

/* IIO read callback */
static int my_read_raw(struct iio_dev *indio_dev,
                       struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask)
{
    struct my_temp_device *dev = iio_priv(indio_dev);
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        ret = my_read_temp(dev, val);
        if (ret)
            return ret;
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        /* Scale: 0.1°C = 100 milli-degrees */
        *val = 100;
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

static const struct iio_info my_iio_info = {
    .read_raw = my_read_raw,
};

/* Probe function */
static int my_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct my_temp_device *my_dev;
    struct iio_dev *indio_dev;
    unsigned int val;
    int ret;

    /* Allocate IIO device with private data */
    indio_dev = devm_iio_device_alloc(dev, sizeof(*my_dev));
    if (!indio_dev)
        return -ENOMEM;

    my_dev = iio_priv(indio_dev);
    my_dev->client = client;

    /* Initialize regmap */
    my_dev->regmap = devm_regmap_init_i2c(client, &my_regmap_config);
    if (IS_ERR(my_dev->regmap)) {
        dev_err(dev, "Failed to init regmap\n");
        return PTR_ERR(my_dev->regmap);
    }

    /* Verify device ID */
    ret = regmap_read(my_dev->regmap, REG_WHO_AM_I, &val);
    if (ret) {
        dev_err(dev, "Failed to read WHO_AM_I\n");
        return ret;
    }

    if (val != WHO_AM_I_VALUE) {
        dev_err(dev, "Unknown device: 0x%02x\n", val);
        return -ENODEV;
    }

    /* Enable sensor */
    ret = regmap_write(my_dev->regmap, REG_CTRL, CTRL_ENABLE);
    if (ret) {
        dev_err(dev, "Failed to enable sensor\n");
        return ret;
    }

    /* Setup IIO device */
    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &my_iio_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = my_channels;
    indio_dev->num_channels = ARRAY_SIZE(my_channels);

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "Failed to register IIO device\n");
        return ret;
    }

    i2c_set_clientdata(client, indio_dev);

    dev_info(dev, "Temperature sensor initialized\n");
    return 0;
}

/* Device tree match table */
static const struct of_device_id my_of_match[] = {
    { .compatible = "vendor,my-temp-sensor" },
    { }
};
MODULE_DEVICE_TABLE(of, my_of_match);

/* I2C ID table */
static const struct i2c_device_id my_id_table[] = {
    { DRIVER_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, my_id_table);

/* I2C driver structure */
static struct i2c_driver my_i2c_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = my_of_match,
    },
    .probe = my_probe,
    .id_table = my_id_table,
};
module_i2c_driver(my_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("I2C Temperature Sensor Driver");
```

---

## Summary

| Function | Purpose |
|----------|---------|
| `module_i2c_driver()` | Register I2C driver |
| `i2c_set_clientdata()` | Store private data |
| `i2c_get_clientdata()` | Retrieve private data |
| `i2c_smbus_read_*()` | SMBus read operations |
| `i2c_smbus_write_*()` | SMBus write operations |
| `i2c_transfer()` | Raw I2C transfers |
| `devm_regmap_init_i2c()` | Initialize I2C regmap |
| `regmap_read/write()` | Regmap operations |

---

**Previous:** [Synchronization](04-synchronization.md) | **Next:** [SPI Subsystem](06-spi-subsystem.md)
