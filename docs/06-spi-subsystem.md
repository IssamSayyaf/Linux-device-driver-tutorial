# SPI Subsystem

## Overview

The SPI (Serial Peripheral Interface) subsystem provides a framework for high-speed synchronous serial communication. This chapter covers writing SPI client drivers, including DMA and interrupt handling.

---

## Table of Contents

1. [SPI Architecture](#spi-architecture)
2. [SPI Driver Structure](#spi-driver-structure)
3. [SPI Transfers](#spi-transfers)
4. [DMA Support](#dma-support)
5. [Interrupt Handling](#interrupt-handling)
6. [Regmap for SPI](#regmap-for-spi)
7. [Complete Example](#complete-example)

---

## SPI Architecture

```
+------------------------------------------------------------------+
|                      SPI SUBSYSTEM ARCHITECTURE                   |
+------------------------------------------------------------------+
|                                                                   |
|   +--------------+   +--------------+   +--------------+          |
|   | SPI Device 1 |   | SPI Device 2 |   | SPI Device 3 |          |
|   | (Your Driver)|   | (Flash)      |   | (Display)    |          |
|   +--------------+   +--------------+   +--------------+          |
|          │                  │                  │                  |
|          └──────────────────┼──────────────────┘                  |
|                             │                                     |
|                             ▼                                     |
|                    +------------------+                           |
|                    | SPI Core         |                           |
|                    | (drivers/spi/)   |                           |
|                    +------------------+                           |
|                             │                                     |
|                             ▼                                     |
|                    +------------------+                           |
|                    | SPI Controller   |                           |
|                    | (Master Driver)  |                           |
|                    +------------------+                           |
|                             │                                     |
|          ┌──────────────────┼──────────────────┐                  |
|          │                  │                  │                  |
|          ▼                  ▼                  ▼                  |
|        MOSI               MISO              SCLK    CS0 CS1 CS2   |
|                                                                   |
+------------------------------------------------------------------+
```

### SPI Signals
| Signal | Description | Direction |
|--------|-------------|-----------|
| MOSI | Master Out Slave In | Master → Slave |
| MISO | Master In Slave Out | Slave → Master |
| SCLK | Serial Clock | Master → Slave |
| CS/SS | Chip Select | Master → Slave |

### SPI Modes
```
+----------+------+------+
| Mode     | CPOL | CPHA |
+----------+------+------+
| Mode 0   |  0   |  0   |
| Mode 1   |  0   |  1   |
| Mode 2   |  1   |  0   |
| Mode 3   |  1   |  1   |
+----------+------+------+

CPOL: Clock polarity (idle state)
CPHA: Clock phase (data sampling edge)
```

---

## SPI Driver Structure

### Essential Headers
```c
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>
```

### SPI Driver Definition
```c
static struct spi_driver my_spi_driver = {
    .driver = {
        .name = "my_spi_device",
        .of_match_table = my_of_match,
        .pm = &my_pm_ops,
    },
    .probe = my_probe,
    .remove = my_remove,
    .id_table = my_spi_id,
};
module_spi_driver(my_spi_driver);
```

### Probe Function
```c
struct my_device {
    struct spi_device *spi;
    struct regmap *regmap;
    /* Device specific data */
};

static int my_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct my_device *my_dev;
    int ret;

    /* Allocate device structure */
    my_dev = devm_kzalloc(dev, sizeof(*my_dev), GFP_KERNEL);
    if (!my_dev)
        return -ENOMEM;

    my_dev->spi = spi;
    spi_set_drvdata(spi, my_dev);

    /* Configure SPI parameters */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 10000000;  /* 10 MHz */

    ret = spi_setup(spi);
    if (ret) {
        dev_err(dev, "SPI setup failed: %d\n", ret);
        return ret;
    }

    /* Initialize device */
    ret = my_device_init(my_dev);
    if (ret)
        return ret;

    dev_info(dev, "SPI device probed\n");
    return 0;
}
```

---

## SPI Transfers

### Simple Read/Write
```c
/*
 * spi_write - Write data to SPI device
 * spi_read - Read data from SPI device
 */

/* Write buffer to device */
static int my_spi_write(struct spi_device *spi, const u8 *buf, size_t len)
{
    return spi_write(spi, buf, len);
}

/* Read buffer from device */
static int my_spi_read(struct spi_device *spi, u8 *buf, size_t len)
{
    return spi_read(spi, buf, len);
}
```

### Write-then-Read
```c
/*
 * spi_write_then_read - Combined write and read
 * Common for reading registers (write address, read data)
 */
static int my_read_register(struct spi_device *spi, u8 reg, u8 *val)
{
    u8 tx = reg | 0x80;  /* Set read bit */
    u8 rx;
    int ret;

    ret = spi_write_then_read(spi, &tx, 1, &rx, 1);
    if (ret)
        return ret;

    *val = rx;
    return 0;
}

static int my_write_register(struct spi_device *spi, u8 reg, u8 val)
{
    u8 tx[2] = { reg & 0x7F, val };  /* Clear read bit */

    return spi_write(spi, tx, 2);
}
```

### SPI Message and Transfer
```c
/*
 * For complex transactions, use spi_message and spi_transfer
 * Provides fine-grained control over transfers
 */

struct spi_transfer {
    const void *tx_buf;     /* Transmit buffer */
    void *rx_buf;           /* Receive buffer */
    unsigned len;           /* Transfer length */

    /* Optional settings */
    unsigned cs_change:1;   /* Deselect after transfer */
    unsigned tx_nbits:3;    /* TX bits per word override */
    unsigned rx_nbits:3;    /* RX bits per word override */
    u8 bits_per_word;       /* Bits per word override */
    u16 delay_usecs;        /* Delay after transfer */
    u32 speed_hz;           /* Speed override */
};
```

### Complex Transfer Example
```c
static int my_complex_transfer(struct spi_device *spi,
                               u8 cmd, u8 *data, size_t len)
{
    struct spi_message msg;
    struct spi_transfer xfers[2];
    int ret;

    memset(xfers, 0, sizeof(xfers));
    spi_message_init(&msg);

    /* First transfer: send command */
    xfers[0].tx_buf = &cmd;
    xfers[0].len = 1;
    spi_message_add_tail(&xfers[0], &msg);

    /* Second transfer: read/write data */
    xfers[1].rx_buf = data;  /* Or tx_buf for write */
    xfers[1].len = len;
    spi_message_add_tail(&xfers[1], &msg);

    ret = spi_sync(spi, &msg);
    if (ret)
        dev_err(&spi->dev, "SPI transfer failed: %d\n", ret);

    return ret;
}
```

### Full-Duplex Transfer
```c
/*
 * Full-duplex: transmit and receive simultaneously
 * Both tx_buf and rx_buf must be same length
 */
static int my_full_duplex(struct spi_device *spi,
                          const u8 *tx, u8 *rx, size_t len)
{
    struct spi_transfer xfer = {
        .tx_buf = tx,
        .rx_buf = rx,
        .len = len,
    };
    struct spi_message msg;

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);

    return spi_sync(spi, &msg);
}
```

---

## DMA Support

### DMA-Capable Buffers
```c
#include <linux/dma-mapping.h>

/*
 * For DMA transfers, buffers must be:
 * - DMA-safe (not on stack)
 * - Properly aligned
 * - Allocated with correct flags
 */

struct my_device {
    struct spi_device *spi;
    u8 *tx_buf;
    u8 *rx_buf;
    dma_addr_t tx_dma;
    dma_addr_t rx_dma;
    size_t buf_size;
};

static int my_alloc_dma_buffers(struct my_device *dev)
{
    struct device *dma_dev = dev->spi->controller->dev.parent;

    dev->buf_size = 4096;

    /* Allocate DMA-coherent buffers */
    dev->tx_buf = dma_alloc_coherent(dma_dev, dev->buf_size,
                                      &dev->tx_dma, GFP_KERNEL);
    if (!dev->tx_buf)
        return -ENOMEM;

    dev->rx_buf = dma_alloc_coherent(dma_dev, dev->buf_size,
                                      &dev->rx_dma, GFP_KERNEL);
    if (!dev->rx_buf) {
        dma_free_coherent(dma_dev, dev->buf_size,
                          dev->tx_buf, dev->tx_dma);
        return -ENOMEM;
    }

    return 0;
}

static void my_free_dma_buffers(struct my_device *dev)
{
    struct device *dma_dev = dev->spi->controller->dev.parent;

    if (dev->tx_buf)
        dma_free_coherent(dma_dev, dev->buf_size,
                          dev->tx_buf, dev->tx_dma);
    if (dev->rx_buf)
        dma_free_coherent(dma_dev, dev->buf_size,
                          dev->rx_buf, dev->rx_dma);
}
```

### Asynchronous SPI with DMA
```c
/*
 * Asynchronous SPI transfer - doesn't block
 * Use completion callback to know when done
 */

static void my_spi_complete(void *context)
{
    struct completion *done = context;
    complete(done);
}

static int my_async_transfer(struct my_device *dev, size_t len)
{
    struct spi_message msg;
    struct spi_transfer xfer = {
        .tx_buf = dev->tx_buf,
        .rx_buf = dev->rx_buf,
        .len = len,
    };
    DECLARE_COMPLETION_ONSTACK(done);
    int ret;

    spi_message_init(&msg);
    spi_message_add_tail(&xfer, &msg);

    msg.complete = my_spi_complete;
    msg.context = &done;

    ret = spi_async(dev->spi, &msg);
    if (ret)
        return ret;

    /* Wait for completion with timeout */
    if (!wait_for_completion_timeout(&done, msecs_to_jiffies(1000))) {
        dev_err(&dev->spi->dev, "SPI transfer timeout\n");
        return -ETIMEDOUT;
    }

    return msg.status;
}
```

---

## Interrupt Handling

### Setting Up Interrupts
```c
#include <linux/interrupt.h>

struct my_device {
    struct spi_device *spi;
    int irq;
    struct completion data_ready;
    /* ... */
};

static irqreturn_t my_irq_handler(int irq, void *dev_id)
{
    struct my_device *dev = dev_id;

    /* Signal data ready */
    complete(&dev->data_ready);

    return IRQ_HANDLED;
}

static int my_probe(struct spi_device *spi)
{
    struct my_device *dev;
    int ret;

    /* ... allocation ... */

    init_completion(&dev->data_ready);

    /* Get IRQ from device tree */
    dev->irq = spi->irq;
    if (dev->irq < 0) {
        /* Or get from GPIO */
        dev->irq = gpiod_to_irq(dev->irq_gpio);
    }

    /* Request IRQ */
    ret = devm_request_irq(&spi->dev, dev->irq, my_irq_handler,
                           IRQF_TRIGGER_FALLING, "my_spi_irq", dev);
    if (ret) {
        dev_err(&spi->dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    return 0;
}
```

### Threaded IRQ Handler
```c
/*
 * For handlers that need to sleep (e.g., SPI communication)
 * Use threaded IRQ handlers
 */

static irqreturn_t my_irq_quick(int irq, void *dev_id)
{
    /* Quick check - runs in hard IRQ context */
    struct my_device *dev = dev_id;

    /* Acknowledge/clear interrupt if needed */
    /* Return IRQ_WAKE_THREAD to run threaded handler */
    return IRQ_WAKE_THREAD;
}

static irqreturn_t my_irq_thread(int irq, void *dev_id)
{
    struct my_device *dev = dev_id;
    int ret;

    /* Can sleep here - read data via SPI */
    ret = my_read_data(dev);
    if (ret)
        dev_err(&dev->spi->dev, "Data read failed\n");

    return IRQ_HANDLED;
}

/* In probe */
ret = devm_request_threaded_irq(&spi->dev, dev->irq,
                                 my_irq_quick,    /* Hard IRQ */
                                 my_irq_thread,   /* Thread function */
                                 IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                 "my_spi_irq", dev);
```

---

## Regmap for SPI

### SPI Regmap Configuration
```c
/*
 * Regmap provides unified register access
 * Handles read/write bit, endianness, caching
 */

/* Standard SPI: address in first byte, R/W bit in MSB */
static const struct regmap_config my_spi_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0x7F,

    /* SPI specific */
    .read_flag_mask = 0x80,     /* Set bit 7 for read */
    .write_flag_mask = 0x00,    /* Clear bit 7 for write */

    /* Caching */
    .cache_type = REGCACHE_RBTREE,
    .volatile_reg = my_volatile_reg,
};

/* 16-bit address, 16-bit value */
static const struct regmap_config my_spi_regmap_16 = {
    .reg_bits = 16,
    .val_bits = 16,
    .max_register = 0xFFFF,
    .reg_format_endian = REGMAP_ENDIAN_BIG,
    .val_format_endian = REGMAP_ENDIAN_BIG,
};
```

### Initialize SPI Regmap
```c
static int my_probe(struct spi_device *spi)
{
    struct my_device *dev;

    dev = devm_kzalloc(&spi->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    dev->regmap = devm_regmap_init_spi(spi, &my_spi_regmap_config);
    if (IS_ERR(dev->regmap)) {
        dev_err(&spi->dev, "Regmap init failed\n");
        return PTR_ERR(dev->regmap);
    }

    /* Use regmap for all register access */
    regmap_write(dev->regmap, REG_CTRL, CTRL_ENABLE);

    return 0;
}
```

---

## Complete Example

### SPI Accelerometer Driver with DMA and Interrupts

```c
/*
 * spi_accel.c - SPI Accelerometer Driver with DMA and Interrupts
 *
 * Demonstrates:
 * - SPI protocol driver
 * - Regmap integration
 * - Threaded IRQ handler
 * - IIO subsystem integration
 * - DMA buffer management
 */

#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/iio/iio.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/iio/trigger_consumer.h>

#define DRIVER_NAME "spi_accel"

/* Register definitions */
#define REG_WHO_AM_I        0x0F
#define REG_CTRL1           0x20
#define REG_CTRL2           0x21
#define REG_STATUS          0x27
#define REG_OUT_X_L         0x28
#define REG_OUT_X_H         0x29
#define REG_OUT_Y_L         0x2A
#define REG_OUT_Y_H         0x2B
#define REG_OUT_Z_L         0x2C
#define REG_OUT_Z_H         0x2D

#define WHO_AM_I_VALUE      0x33
#define CTRL1_ODR_100HZ     0x57  /* 100 Hz, all axes enabled */
#define STATUS_ZYXDA        BIT(3)

/* Device structure */
struct spi_accel {
    struct spi_device *spi;
    struct regmap *regmap;
    struct iio_trigger *trig;
    int irq;

    /* DMA-safe buffer for bulk reads */
    u8 data_buffer[8] __aligned(IIO_DMA_MINALIGN);

    /* Timestamp for buffer */
    s64 timestamp;
};

/* Regmap configuration */
static const struct regmap_config spi_accel_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0x3F,
    .read_flag_mask = 0x80,     /* Set for read */
};

/* IIO channel definitions */
#define SPI_ACCEL_CHANNEL(axis, index)                          \
{                                                               \
    .type = IIO_ACCEL,                                          \
    .modified = 1,                                              \
    .channel2 = IIO_MOD_##axis,                                 \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),               \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),       \
    .scan_index = index,                                        \
    .scan_type = {                                              \
        .sign = 's',                                            \
        .realbits = 16,                                         \
        .storagebits = 16,                                      \
        .shift = 0,                                             \
        .endianness = IIO_LE,                                   \
    },                                                          \
}

static const struct iio_chan_spec spi_accel_channels[] = {
    SPI_ACCEL_CHANNEL(X, 0),
    SPI_ACCEL_CHANNEL(Y, 1),
    SPI_ACCEL_CHANNEL(Z, 2),
    IIO_CHAN_SOFT_TIMESTAMP(3),
};

/* Read single axis */
static int spi_accel_read_axis(struct spi_accel *accel, int axis, s16 *val)
{
    u8 data[2];
    int ret;

    /* Read 2 bytes starting from axis register */
    ret = regmap_bulk_read(accel->regmap,
                           REG_OUT_X_L + (axis * 2),
                           data, 2);
    if (ret)
        return ret;

    *val = (s16)((data[1] << 8) | data[0]);
    return 0;
}

/* Read all axes (for triggered buffer) */
static int spi_accel_read_all(struct spi_accel *accel, s16 *buf)
{
    int ret;

    /* Bulk read all 6 bytes */
    ret = regmap_bulk_read(accel->regmap, REG_OUT_X_L,
                           accel->data_buffer, 6);
    if (ret)
        return ret;

    /* Convert to s16 array */
    buf[0] = (s16)((accel->data_buffer[1] << 8) | accel->data_buffer[0]);
    buf[1] = (s16)((accel->data_buffer[3] << 8) | accel->data_buffer[2]);
    buf[2] = (s16)((accel->data_buffer[5] << 8) | accel->data_buffer[4]);

    return 0;
}

/* IIO read_raw callback */
static int spi_accel_read_raw(struct iio_dev *indio_dev,
                              struct iio_chan_spec const *chan,
                              int *val, int *val2, long mask)
{
    struct spi_accel *accel = iio_priv(indio_dev);
    s16 raw;
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        ret = iio_device_claim_direct_mode(indio_dev);
        if (ret)
            return ret;

        ret = spi_accel_read_axis(accel, chan->scan_index, &raw);
        iio_device_release_direct_mode(indio_dev);

        if (ret)
            return ret;

        *val = raw;
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        /* Scale: 4mg per LSB = 0.004 * 9.80665 m/s² */
        *val = 0;
        *val2 = 39226;  /* 0.039226 m/s² */
        return IIO_VAL_INT_PLUS_MICRO;

    default:
        return -EINVAL;
    }
}

/* Triggered buffer handler */
static irqreturn_t spi_accel_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct spi_accel *accel = iio_priv(indio_dev);
    s16 buf[8];  /* 3 channels + padding + timestamp */
    int ret;

    ret = spi_accel_read_all(accel, buf);
    if (ret)
        goto done;

    iio_push_to_buffers_with_timestamp(indio_dev, buf,
                                       iio_get_time_ns(indio_dev));

done:
    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}

/* Data ready IRQ handler */
static irqreturn_t spi_accel_data_ready(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct spi_accel *accel = iio_priv(indio_dev);

    if (accel->trig)
        iio_trigger_poll(accel->trig);

    return IRQ_HANDLED;
}

/* IIO info structure */
static const struct iio_info spi_accel_info = {
    .read_raw = spi_accel_read_raw,
};

/* Trigger ops */
static const struct iio_trigger_ops spi_accel_trigger_ops = {
};

/* Initialize device hardware */
static int spi_accel_hw_init(struct spi_accel *accel)
{
    unsigned int val;
    int ret;

    /* Read and verify WHO_AM_I */
    ret = regmap_read(accel->regmap, REG_WHO_AM_I, &val);
    if (ret) {
        dev_err(&accel->spi->dev, "Failed to read WHO_AM_I\n");
        return ret;
    }

    if (val != WHO_AM_I_VALUE) {
        dev_err(&accel->spi->dev, "Unknown device: 0x%02x\n", val);
        return -ENODEV;
    }

    /* Configure device: 100 Hz, all axes enabled */
    ret = regmap_write(accel->regmap, REG_CTRL1, CTRL1_ODR_100HZ);
    if (ret) {
        dev_err(&accel->spi->dev, "Failed to configure device\n");
        return ret;
    }

    return 0;
}

/* Probe function */
static int spi_accel_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct iio_dev *indio_dev;
    struct spi_accel *accel;
    int ret;

    /* Allocate IIO device */
    indio_dev = devm_iio_device_alloc(dev, sizeof(*accel));
    if (!indio_dev)
        return -ENOMEM;

    accel = iio_priv(indio_dev);
    accel->spi = spi;
    accel->irq = spi->irq;

    spi_set_drvdata(spi, indio_dev);

    /* Configure SPI */
    spi->mode = SPI_MODE_3;
    spi->bits_per_word = 8;
    spi->max_speed_hz = 10000000;

    ret = spi_setup(spi);
    if (ret) {
        dev_err(dev, "SPI setup failed\n");
        return ret;
    }

    /* Initialize regmap */
    accel->regmap = devm_regmap_init_spi(spi, &spi_accel_regmap_config);
    if (IS_ERR(accel->regmap)) {
        dev_err(dev, "Regmap init failed\n");
        return PTR_ERR(accel->regmap);
    }

    /* Initialize hardware */
    ret = spi_accel_hw_init(accel);
    if (ret)
        return ret;

    /* Setup IIO device */
    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &spi_accel_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = spi_accel_channels;
    indio_dev->num_channels = ARRAY_SIZE(spi_accel_channels);

    /* Setup triggered buffer */
    ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
                                          iio_pollfunc_store_time,
                                          spi_accel_trigger_handler,
                                          NULL);
    if (ret) {
        dev_err(dev, "Buffer setup failed\n");
        return ret;
    }

    /* Setup data-ready trigger if IRQ available */
    if (accel->irq > 0) {
        accel->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
                                              indio_dev->name,
                                              iio_device_id(indio_dev));
        if (!accel->trig)
            return -ENOMEM;

        accel->trig->ops = &spi_accel_trigger_ops;
        iio_trigger_set_drvdata(accel->trig, indio_dev);

        ret = devm_iio_trigger_register(dev, accel->trig);
        if (ret)
            return ret;

        ret = devm_request_irq(dev, accel->irq, spi_accel_data_ready,
                               IRQF_TRIGGER_RISING,
                               DRIVER_NAME, indio_dev);
        if (ret) {
            dev_err(dev, "IRQ request failed\n");
            return ret;
        }

        indio_dev->trig = iio_trigger_get(accel->trig);
    }

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "IIO registration failed\n");
        return ret;
    }

    dev_info(dev, "SPI accelerometer initialized\n");
    return 0;
}

/* Device tree match */
static const struct of_device_id spi_accel_of_match[] = {
    { .compatible = "vendor,spi-accel" },
    { }
};
MODULE_DEVICE_TABLE(of, spi_accel_of_match);

/* SPI device ID */
static const struct spi_device_id spi_accel_id[] = {
    { DRIVER_NAME, 0 },
    { }
};
MODULE_DEVICE_TABLE(spi, spi_accel_id);

/* SPI driver */
static struct spi_driver spi_accel_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = spi_accel_of_match,
    },
    .probe = spi_accel_probe,
    .id_table = spi_accel_id,
};
module_spi_driver(spi_accel_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("SPI Accelerometer Driver with DMA and Interrupts");
```

---

## Device Tree Example

```dts
&spi0 {
    status = "okay";

    accel@0 {
        compatible = "vendor,spi-accel";
        reg = <0>;                      /* CS 0 */
        spi-max-frequency = <10000000>; /* 10 MHz */
        spi-cpol;                       /* Mode 3 */
        spi-cpha;

        interrupt-parent = <&gpio1>;
        interrupts = <8 IRQ_TYPE_EDGE_RISING>;
    };
};
```

---

## Summary

| Function | Purpose |
|----------|---------|
| `module_spi_driver()` | Register SPI driver |
| `spi_setup()` | Configure SPI parameters |
| `spi_write()` | Write data to device |
| `spi_read()` | Read data from device |
| `spi_write_then_read()` | Combined write-read |
| `spi_sync()` | Synchronous message transfer |
| `spi_async()` | Asynchronous transfer |
| `devm_regmap_init_spi()` | Initialize SPI regmap |

---

**Previous:** [I2C Subsystem](05-i2c-subsystem.md) | **Next:** [UART Subsystem](07-uart-subsystem.md)
