# IIO (Industrial I/O) Subsystem

## Overview

The IIO subsystem is the standard framework for sensors in Linux. It provides a unified interface for accelerometers, gyroscopes, ADCs, DACs, temperature sensors, and more. This is the subsystem you should use for most sensor drivers.

---

## Table of Contents

1. [IIO Architecture](#iio-architecture)
2. [When to Use IIO](#when-to-use-iio)
3. [IIO Channel Types](#iio-channel-types)
4. [Creating IIO Drivers](#creating-iio-drivers)
5. [Channel Definitions](#channel-definitions)
6. [Buffers and Triggers](#buffers-and-triggers)
7. [Events](#events)
8. [Complete Examples](#complete-examples)

---

## IIO Architecture

```
+------------------------------------------------------------------+
|                      IIO SUBSYSTEM ARCHITECTURE                   |
+------------------------------------------------------------------+
|                                                                   |
|   Userspace Access Methods:                                       |
|   +-----------------+  +------------------+  +------------------+ |
|   | /sys/bus/iio/   |  | /dev/iio:deviceX |  | /dev/iio:eventX  | |
|   | devices/        |  | (character dev)  |  | (event dev)      | |
|   +-----------------+  +------------------+  +------------------+ |
|          │                    │                    │             |
|   ───────┼────────────────────┼────────────────────┼──────────   |
|          │                    │                    │             |
|   Kernel │                    │                    │             |
|          ▼                    ▼                    ▼             |
|   +----------------------------------------------------------+  |
|   |                    IIO Core                               |  |
|   | - Channel management                                      |  |
|   | - Sysfs attribute generation                              |  |
|   | - Buffer/trigger infrastructure                           |  |
|   | - Event handling                                          |  |
|   +----------------------------------------------------------+  |
|                              │                                   |
|                              ▼                                   |
|   +----------------------------------------------------------+  |
|   |                   Your IIO Driver                         |  |
|   | - Hardware communication                                  |  |
|   | - Data conversion                                         |  |
|   | - Interrupt handling                                      |  |
|   +----------------------------------------------------------+  |
|                                                                   |
+------------------------------------------------------------------+
```

---

## When to Use IIO

### IIO is for:
| Device Type | Examples |
|-------------|----------|
| Accelerometers | MPU6050, ADXL345, LIS3DH |
| Gyroscopes | MPU6050, L3GD20 |
| Magnetometers | HMC5883L, AK8963 |
| ADCs | MCP3008, ADS1115 |
| DACs | MCP4725 |
| Temperature sensors | TMP102, LM75 |
| Pressure sensors | BMP280, MS5611 |
| Light sensors | TSL2561, VEML6070 |
| Proximity sensors | VCNL4000 |
| Humidity sensors | HDC1080, SHT31 |
| Chemical sensors | CCS811 |

### NOT IIO - Use Other Subsystems:
| Device Type | Subsystem |
|-------------|-----------|
| GPS modules | GNSS |
| Displays | DRM/FB |
| Touch screens | Input |
| Keyboards | Input |
| Network interfaces | Net |
| Audio devices | ALSA |
| Storage | Block |

---

## IIO Channel Types

### Common Channel Types
```c
#include <linux/iio/types.h>

/* Motion sensors */
IIO_ACCEL          /* Acceleration (m/s²) */
IIO_ANGL_VEL       /* Angular velocity (rad/s) */
IIO_MAGN           /* Magnetic field (Gauss) */
IIO_INCLI          /* Inclinometer (degrees) */
IIO_ROT            /* Rotation (quaternion) */

/* Environmental sensors */
IIO_TEMP           /* Temperature (milli°C) */
IIO_PRESSURE       /* Pressure (kPa) */
IIO_HUMIDITYRELATIVE /* Relative humidity (%) */
IIO_LIGHT          /* Ambient light (lux) */
IIO_PROXIMITY      /* Proximity (arbitrary) */

/* Electrical measurements */
IIO_VOLTAGE        /* Voltage (mV) */
IIO_CURRENT        /* Current (mA) */
IIO_POWER          /* Power (mW) */
IIO_RESISTANCE     /* Resistance (Ohm) */

/* Analog conversion */
IIO_ALTVOLTAGE     /* Analog output (DAC) */

/* Position/Distance */
IIO_DISTANCE       /* Distance (mm) */
IIO_POSITIONRELATIVE /* Relative position */

/* Chemical */
IIO_CONCENTRATION  /* Concentration (ppm) */
```

### Channel Modifiers
```c
/* Axis modifiers for multi-axis sensors */
IIO_MOD_X          /* X axis */
IIO_MOD_Y          /* Y axis */
IIO_MOD_Z          /* Z axis */

/* Temperature modifiers */
IIO_MOD_TEMP_AMBIENT    /* Ambient temperature */
IIO_MOD_TEMP_OBJECT     /* Object temperature (IR) */

/* Light modifiers */
IIO_MOD_LIGHT_IR        /* Infrared */
IIO_MOD_LIGHT_UV        /* Ultraviolet */
IIO_MOD_LIGHT_BOTH      /* Combined */

/* Color modifiers */
IIO_MOD_LIGHT_RED
IIO_MOD_LIGHT_GREEN
IIO_MOD_LIGHT_BLUE
IIO_MOD_LIGHT_CLEAR
```

---

## Creating IIO Drivers

### Essential Headers
```c
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
```

### IIO Device Structure
```c
struct my_sensor {
    struct i2c_client *client;  /* or spi_device */
    struct regmap *regmap;

    /* Calibration data */
    s16 offset_x, offset_y, offset_z;

    /* Runtime state */
    int sample_rate;
    int scale;

    /* Buffer */
    s16 buffer[8] __aligned(IIO_DMA_MINALIGN);
    s64 timestamp;
};
```

### Allocating IIO Device
```c
static int my_probe(struct i2c_client *client)
{
    struct iio_dev *indio_dev;
    struct my_sensor *sensor;

    /* Allocate IIO device with private data */
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*sensor));
    if (!indio_dev)
        return -ENOMEM;

    /* Get pointer to private data */
    sensor = iio_priv(indio_dev);
    sensor->client = client;

    /* Store reference */
    i2c_set_clientdata(client, indio_dev);

    /* Configure IIO device */
    indio_dev->name = "my_sensor";
    indio_dev->info = &my_sensor_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = my_channels;
    indio_dev->num_channels = ARRAY_SIZE(my_channels);

    /* Register device */
    return devm_iio_device_register(&client->dev, indio_dev);
}
```

---

## Channel Definitions

### Basic Channel Definition
```c
/*
 * struct iio_chan_spec - IIO channel specification
 *
 * Defines how the channel appears in sysfs and how to read it
 */

static const struct iio_chan_spec simple_channel = {
    .type = IIO_TEMP,               /* Channel type */
    .info_mask_separate =           /* Per-channel attributes */
        BIT(IIO_CHAN_INFO_RAW) |    /* in_temp_raw */
        BIT(IIO_CHAN_INFO_OFFSET),  /* in_temp_offset */
    .info_mask_shared_by_type =     /* Shared between same type */
        BIT(IIO_CHAN_INFO_SCALE),   /* in_temp_scale */
};
```

### Multi-Axis Channel (Macro)
```c
#define MY_ACCEL_CHANNEL(axis, idx)                             \
{                                                               \
    .type = IIO_ACCEL,                                          \
    .modified = 1,                                              \
    .channel2 = IIO_MOD_##axis,                                 \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |              \
                          BIT(IIO_CHAN_INFO_CALIBBIAS),         \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |      \
                                BIT(IIO_CHAN_INFO_SAMP_FREQ),   \
    .scan_index = idx,                                          \
    .scan_type = {                                              \
        .sign = 's',                                            \
        .realbits = 16,                                         \
        .storagebits = 16,                                      \
        .endianness = IIO_LE,                                   \
    },                                                          \
}

static const struct iio_chan_spec my_accel_channels[] = {
    MY_ACCEL_CHANNEL(X, 0),
    MY_ACCEL_CHANNEL(Y, 1),
    MY_ACCEL_CHANNEL(Z, 2),
    IIO_CHAN_SOFT_TIMESTAMP(3),  /* Buffer timestamp */
};
```

### Info Mask Attributes
```c
/*
 * Info masks determine what sysfs attributes are created:
 *
 * IIO_CHAN_INFO_RAW        - Raw value from sensor
 * IIO_CHAN_INFO_PROCESSED  - Processed value (real units)
 * IIO_CHAN_INFO_SCALE      - Scale factor
 * IIO_CHAN_INFO_OFFSET     - Offset for conversion
 * IIO_CHAN_INFO_CALIBSCALE - Calibration scale
 * IIO_CHAN_INFO_CALIBBIAS  - Calibration offset
 * IIO_CHAN_INFO_SAMP_FREQ  - Sample frequency
 * IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY
 * IIO_CHAN_INFO_OVERSAMPLING_RATIO
 */

/* Sysfs attribute naming:
 *
 * For type = IIO_ACCEL, modifier = IIO_MOD_X:
 * - in_accel_x_raw
 * - in_accel_x_scale (if separate)
 * - in_accel_scale (if shared_by_type)
 *
 * For type = IIO_TEMP (no modifier):
 * - in_temp_raw
 * - in_temp_scale
 */
```

### Implementing Read Callbacks
```c
/*
 * struct iio_info - IIO device operations
 */
static int my_read_raw(struct iio_dev *indio_dev,
                       struct iio_chan_spec const *chan,
                       int *val, int *val2, long mask)
{
    struct my_sensor *sensor = iio_priv(indio_dev);
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        /* Read raw value from hardware */
        ret = iio_device_claim_direct_mode(indio_dev);
        if (ret)
            return ret;

        switch (chan->type) {
        case IIO_ACCEL:
            ret = my_read_accel(sensor, chan->channel2, val);
            break;
        case IIO_TEMP:
            ret = my_read_temp(sensor, val);
            break;
        default:
            ret = -EINVAL;
        }

        iio_device_release_direct_mode(indio_dev);
        return ret < 0 ? ret : IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        /* Return scale factor
         * Final value = raw * val.val2 / 10^6
         * e.g., 0.000598 = 0 + 598/1000000
         */
        *val = 0;
        *val2 = 598;  /* 0.000598 */
        return IIO_VAL_INT_PLUS_MICRO;

    case IIO_CHAN_INFO_OFFSET:
        /* Temperature offset for conversion */
        *val = -4000;  /* -40°C offset */
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SAMP_FREQ:
        *val = sensor->sample_rate;
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

static int my_write_raw(struct iio_dev *indio_dev,
                        struct iio_chan_spec const *chan,
                        int val, int val2, long mask)
{
    struct my_sensor *sensor = iio_priv(indio_dev);

    switch (mask) {
    case IIO_CHAN_INFO_SAMP_FREQ:
        return my_set_sample_rate(sensor, val);

    case IIO_CHAN_INFO_CALIBBIAS:
        return my_set_calibbias(sensor, chan->channel2, val);

    default:
        return -EINVAL;
    }
}

static const struct iio_info my_sensor_info = {
    .read_raw = my_read_raw,
    .write_raw = my_write_raw,
};
```

### Return Value Types
```c
/*
 * IIO_VAL_* specifies how val and val2 are interpreted:
 *
 * IIO_VAL_INT           - Integer: val
 * IIO_VAL_INT_PLUS_MICRO - val + val2/1000000
 * IIO_VAL_INT_PLUS_NANO  - val + val2/1000000000
 * IIO_VAL_FRACTIONAL    - val/val2
 * IIO_VAL_FRACTIONAL_LOG2 - val / 2^val2
 *
 * Examples:
 * Temperature 23.456°C:
 *   *val = 23, *val2 = 456000, return IIO_VAL_INT_PLUS_MICRO
 *
 * Scale 0.000061:
 *   *val = 0, *val2 = 61, return IIO_VAL_INT_PLUS_MICRO
 *
 * Scale 1/16384:
 *   *val = 1, *val2 = 16384, return IIO_VAL_FRACTIONAL
 */
```

---

## Buffers and Triggers

### Triggered Buffer Setup
```c
/*
 * Triggered buffers allow efficient continuous data capture
 * Data is pushed to a buffer and read in bulk from userspace
 */

#include <linux/iio/triggered_buffer.h>

static irqreturn_t my_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct my_sensor *sensor = iio_priv(indio_dev);
    int ret;

    /* Read all channels */
    ret = my_read_all_data(sensor, sensor->buffer);
    if (ret)
        goto done;

    /* Push to buffer with timestamp */
    iio_push_to_buffers_with_timestamp(indio_dev,
                                       sensor->buffer,
                                       iio_get_time_ns(indio_dev));

done:
    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}

static int my_probe(struct i2c_client *client)
{
    struct iio_dev *indio_dev;
    int ret;

    /* ... allocation ... */

    /* Setup triggered buffer */
    ret = devm_iio_triggered_buffer_setup(&client->dev,
                                          indio_dev,
                                          iio_pollfunc_store_time,
                                          my_trigger_handler,
                                          NULL);
    if (ret)
        return ret;

    /* ... register device ... */
}
```

### Creating a Device Trigger
```c
/*
 * Hardware trigger (e.g., data-ready interrupt)
 */

static const struct iio_trigger_ops my_trigger_ops = {
    .set_trigger_state = my_set_trigger_state,
};

static int my_set_trigger_state(struct iio_trigger *trig, bool state)
{
    struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
    struct my_sensor *sensor = iio_priv(indio_dev);

    if (state)
        return my_enable_drdy_irq(sensor);
    else
        return my_disable_drdy_irq(sensor);
}

static irqreturn_t my_data_ready_irq(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct my_sensor *sensor = iio_priv(indio_dev);

    if (sensor->trig)
        iio_trigger_poll(sensor->trig);

    return IRQ_HANDLED;
}

static int my_probe_trigger(struct iio_dev *indio_dev)
{
    struct my_sensor *sensor = iio_priv(indio_dev);
    struct device *dev = &sensor->client->dev;
    int ret;

    sensor->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
                                          indio_dev->name,
                                          iio_device_id(indio_dev));
    if (!sensor->trig)
        return -ENOMEM;

    sensor->trig->ops = &my_trigger_ops;
    iio_trigger_set_drvdata(sensor->trig, indio_dev);

    ret = devm_iio_trigger_register(dev, sensor->trig);
    if (ret)
        return ret;

    /* Set as default trigger */
    indio_dev->trig = iio_trigger_get(sensor->trig);

    return 0;
}
```

### Reading from Buffer (Userspace)
```c
/* Userspace application */
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/iio/buffer.h>

int main(void)
{
    int fd, ret;
    char buf[24];  /* 3 x int16 + padding + timestamp */

    /* Enable channels */
    system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_x_en");
    system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_y_en");
    system("echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_z_en");

    /* Set buffer length */
    system("echo 100 > /sys/bus/iio/devices/iio:device0/buffer/length");

    /* Enable buffer */
    system("echo 1 > /sys/bus/iio/devices/iio:device0/buffer/enable");

    /* Open character device */
    fd = open("/dev/iio:device0", O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    /* Read samples */
    while (1) {
        ret = read(fd, buf, sizeof(buf));
        if (ret < 0) {
            perror("read");
            break;
        }

        /* Parse data (depends on scan_type) */
        int16_t *data = (int16_t *)buf;
        printf("X: %d, Y: %d, Z: %d\n", data[0], data[1], data[2]);
    }

    close(fd);
    return 0;
}
```

---

## Events

### Threshold Events
```c
/*
 * IIO events notify userspace of threshold crossings,
 * motion detection, etc.
 */

#include <linux/iio/events.h>

/* Channel with event support */
static const struct iio_chan_spec accel_channel = {
    .type = IIO_ACCEL,
    .modified = 1,
    .channel2 = IIO_MOD_X,
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    .event_spec = accel_events,
    .num_event_specs = ARRAY_SIZE(accel_events),
};

/* Event specification */
static const struct iio_event_spec accel_events[] = {
    {
        .type = IIO_EV_TYPE_THRESH,
        .dir = IIO_EV_DIR_RISING,
        .mask_separate = BIT(IIO_EV_INFO_ENABLE) |
                         BIT(IIO_EV_INFO_VALUE),
    },
    {
        .type = IIO_EV_TYPE_THRESH,
        .dir = IIO_EV_DIR_FALLING,
        .mask_separate = BIT(IIO_EV_INFO_ENABLE) |
                         BIT(IIO_EV_INFO_VALUE),
    },
};

/* Event callbacks */
static int my_read_event_config(struct iio_dev *indio_dev,
                                const struct iio_chan_spec *chan,
                                enum iio_event_type type,
                                enum iio_event_direction dir)
{
    struct my_sensor *sensor = iio_priv(indio_dev);
    return sensor->events_enabled;
}

static int my_write_event_config(struct iio_dev *indio_dev,
                                 const struct iio_chan_spec *chan,
                                 enum iio_event_type type,
                                 enum iio_event_direction dir,
                                 int state)
{
    struct my_sensor *sensor = iio_priv(indio_dev);
    sensor->events_enabled = state;
    return my_configure_events(sensor, state);
}

static int my_read_event_value(struct iio_dev *indio_dev,
                               const struct iio_chan_spec *chan,
                               enum iio_event_type type,
                               enum iio_event_direction dir,
                               enum iio_event_info info,
                               int *val, int *val2)
{
    struct my_sensor *sensor = iio_priv(indio_dev);
    *val = sensor->threshold;
    return IIO_VAL_INT;
}

/* Push event to userspace */
static irqreturn_t my_event_irq(int irq, void *private)
{
    struct iio_dev *indio_dev = private;

    iio_push_event(indio_dev,
                   IIO_UNMOD_EVENT_CODE(IIO_ACCEL,
                                        0,
                                        IIO_EV_TYPE_THRESH,
                                        IIO_EV_DIR_RISING),
                   iio_get_time_ns(indio_dev));

    return IRQ_HANDLED;
}
```

---

## Complete Examples

### Example 1: Simple Temperature Sensor

```c
/*
 * simple_temp.c - Simple IIO Temperature Sensor Driver
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>

#define DRIVER_NAME "simple_temp"
#define REG_TEMP    0x00

struct simple_temp {
    struct regmap *regmap;
};

static int simple_temp_read_raw(struct iio_dev *indio_dev,
                                struct iio_chan_spec const *chan,
                                int *val, int *val2, long mask)
{
    struct simple_temp *st = iio_priv(indio_dev);
    unsigned int reg_val;
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        ret = regmap_read(st->regmap, REG_TEMP, &reg_val);
        if (ret)
            return ret;
        *val = (s16)reg_val;
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        /* 62.5 milli-degrees per LSB */
        *val = 62;
        *val2 = 500000;
        return IIO_VAL_INT_PLUS_MICRO;

    default:
        return -EINVAL;
    }
}

static const struct iio_chan_spec simple_temp_channels[] = {
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),
    },
};

static const struct iio_info simple_temp_info = {
    .read_raw = simple_temp_read_raw,
};

static const struct regmap_config simple_temp_regmap = {
    .reg_bits = 8,
    .val_bits = 16,
    .max_register = 0x03,
};

static int simple_temp_probe(struct i2c_client *client)
{
    struct iio_dev *indio_dev;
    struct simple_temp *st;

    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*st));
    if (!indio_dev)
        return -ENOMEM;

    st = iio_priv(indio_dev);

    st->regmap = devm_regmap_init_i2c(client, &simple_temp_regmap);
    if (IS_ERR(st->regmap))
        return PTR_ERR(st->regmap);

    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &simple_temp_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = simple_temp_channels;
    indio_dev->num_channels = ARRAY_SIZE(simple_temp_channels);

    return devm_iio_device_register(&client->dev, indio_dev);
}

static const struct of_device_id simple_temp_of_match[] = {
    { .compatible = "vendor,simple-temp" },
    { }
};
MODULE_DEVICE_TABLE(of, simple_temp_of_match);

static struct i2c_driver simple_temp_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = simple_temp_of_match,
    },
    .probe = simple_temp_probe,
};
module_i2c_driver(simple_temp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Simple IIO Temperature Sensor");
```

---

## Sysfs Interface Reference

### Standard Sysfs Attributes
```bash
/sys/bus/iio/devices/iio:device0/
├── name                    # Device name
├── dev                     # Major:minor numbers
├── in_accel_x_raw          # Raw X acceleration
├── in_accel_y_raw          # Raw Y acceleration
├── in_accel_z_raw          # Raw Z acceleration
├── in_accel_scale          # Scale factor
├── in_accel_sampling_frequency  # Sample rate
├── in_temp_raw             # Raw temperature
├── in_temp_scale           # Temperature scale
├── buffer/
│   ├── enable              # Enable/disable buffer
│   ├── length              # Buffer size
│   └── watermark           # Buffer watermark
├── scan_elements/
│   ├── in_accel_x_en       # Enable X in scan
│   ├── in_accel_x_index    # X position in scan
│   └── in_accel_x_type     # X data format
└── trigger/
    └── current_trigger     # Active trigger name
```

### Reading Values from Userspace
```bash
# Direct reading
cat /sys/bus/iio/devices/iio:device0/in_accel_x_raw
cat /sys/bus/iio/devices/iio:device0/in_accel_scale

# Calculate real value:
# accel_m_s2 = raw * scale

# Set sample frequency
echo 100 > /sys/bus/iio/devices/iio:device0/in_accel_sampling_frequency
```

---

## Summary

| Component | Purpose |
|-----------|---------|
| `iio_chan_spec` | Channel definition |
| `iio_info` | Read/write callbacks |
| `iio_trigger` | Data capture trigger |
| `iio_buffer` | Continuous data buffer |
| `iio_event` | Threshold notifications |
| `devm_iio_device_alloc()` | Allocate IIO device |
| `devm_iio_device_register()` | Register IIO device |
| `iio_push_to_buffers()` | Push data to buffer |
| `iio_push_event()` | Push event to userspace |

---

**Previous:** [UART Subsystem](07-uart-subsystem.md) | **Next:** [GPIO Subsystem](09-gpio-subsystem.md)
