// SPDX-License-Identifier: GPL-2.0-only
/*
 * mpu6050.c - InvenSense MPU6050 6-Axis Accelerometer/Gyroscope Driver
 *
 * This driver demonstrates:
 * - I2C client driver
 * - IIO subsystem integration
 * - Triggered buffer support (optional via Kconfig)
 * - Interrupt handling (optional via Kconfig)
 * - DMA support (optional via Kconfig)
 * - FIFO buffering (optional via Kconfig)
 * - Power management
 * - Device tree configuration
 *
 * Kconfig options:
 * - CONFIG_MPU6050: Enable the driver (tristate: y/m/n)
 * - CONFIG_MPU6050_IRQ: Enable interrupt support
 * - CONFIG_MPU6050_TRIGGER: Enable hardware trigger
 * - CONFIG_MPU6050_DMA: Enable DMA buffer support
 * - CONFIG_MPU6050_FIFO: Use internal FIFO
 * - CONFIG_MPU6050_DEBUG: Enable debug output
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/pm_runtime.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>

/* Conditional includes based on Kconfig */
#ifdef CONFIG_MPU6050_IRQ
#include <linux/interrupt.h>
#endif

#ifdef CONFIG_MPU6050_TRIGGER
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#endif

#ifdef CONFIG_MPU6050_DMA
#include <linux/dma-mapping.h>
#endif

#define DRIVER_NAME "mpu6050"

/*
 * Debug macros - only enabled with CONFIG_MPU6050_DEBUG
 */
#ifdef CONFIG_MPU6050_DEBUG
#define mpu_dbg(dev, fmt, ...) \
    dev_dbg(dev, "[MPU6050] " fmt, ##__VA_ARGS__)
#define mpu_dbg_reg(dev, reg, val) \
    dev_dbg(dev, "[MPU6050] Reg 0x%02x = 0x%02x\n", reg, val)
#else
#define mpu_dbg(dev, fmt, ...) do { } while (0)
#define mpu_dbg_reg(dev, reg, val) do { } while (0)
#endif

/*
 * ============================================================
 * Register Map
 * ============================================================
 */

/* Configuration registers */
#define MPU6050_REG_SMPLRT_DIV      0x19    /* Sample Rate Divider */
#define MPU6050_REG_CONFIG          0x1A    /* Configuration */
#define MPU6050_REG_GYRO_CONFIG     0x1B    /* Gyroscope Configuration */
#define MPU6050_REG_ACCEL_CONFIG    0x1C    /* Accelerometer Configuration */
#define MPU6050_REG_FIFO_EN         0x23    /* FIFO Enable */

/* Interrupt registers */
#define MPU6050_REG_INT_PIN_CFG     0x37    /* Interrupt Pin Configuration */
#define MPU6050_REG_INT_ENABLE      0x38    /* Interrupt Enable */
#define MPU6050_REG_INT_STATUS      0x3A    /* Interrupt Status */

/* Data registers */
#define MPU6050_REG_ACCEL_XOUT_H    0x3B    /* Accelerometer X high byte */
#define MPU6050_REG_ACCEL_XOUT_L    0x3C
#define MPU6050_REG_ACCEL_YOUT_H    0x3D
#define MPU6050_REG_ACCEL_YOUT_L    0x3E
#define MPU6050_REG_ACCEL_ZOUT_H    0x3F
#define MPU6050_REG_ACCEL_ZOUT_L    0x40
#define MPU6050_REG_TEMP_OUT_H      0x41    /* Temperature high byte */
#define MPU6050_REG_TEMP_OUT_L      0x42
#define MPU6050_REG_GYRO_XOUT_H     0x43    /* Gyroscope X high byte */
#define MPU6050_REG_GYRO_XOUT_L     0x44
#define MPU6050_REG_GYRO_YOUT_H     0x45
#define MPU6050_REG_GYRO_YOUT_L     0x46
#define MPU6050_REG_GYRO_ZOUT_H     0x47
#define MPU6050_REG_GYRO_ZOUT_L     0x48

/* FIFO registers */
#define MPU6050_REG_USER_CTRL       0x6A    /* User Control */
#define MPU6050_REG_FIFO_COUNT_H    0x72    /* FIFO Count High */
#define MPU6050_REG_FIFO_COUNT_L    0x73    /* FIFO Count Low */
#define MPU6050_REG_FIFO_R_W        0x74    /* FIFO Read/Write */

/* Power management */
#define MPU6050_REG_PWR_MGMT_1      0x6B    /* Power Management 1 */
#define MPU6050_REG_PWR_MGMT_2      0x6C    /* Power Management 2 */

/* Identification */
#define MPU6050_REG_WHO_AM_I        0x75    /* Device ID */

/*
 * ============================================================
 * Register Bits
 * ============================================================
 */

/* PWR_MGMT_1 */
#define MPU6050_PWR1_DEVICE_RESET   BIT(7)
#define MPU6050_PWR1_SLEEP          BIT(6)
#define MPU6050_PWR1_CYCLE          BIT(5)
#define MPU6050_PWR1_TEMP_DIS       BIT(3)
#define MPU6050_PWR1_CLKSEL_MASK    0x07
#define MPU6050_PWR1_CLKSEL_PLL_X   0x01    /* PLL with X gyro reference */

/* USER_CTRL */
#define MPU6050_USR_FIFO_EN         BIT(6)
#define MPU6050_USR_FIFO_RST        BIT(2)

/* FIFO_EN */
#define MPU6050_FIFO_TEMP_EN        BIT(7)
#define MPU6050_FIFO_XG_EN          BIT(6)
#define MPU6050_FIFO_YG_EN          BIT(5)
#define MPU6050_FIFO_ZG_EN          BIT(4)
#define MPU6050_FIFO_ACCEL_EN       BIT(3)

/* GYRO_CONFIG */
#define MPU6050_GYRO_FS_SEL_SHIFT   3
#define MPU6050_GYRO_FS_250         0x00    /* ±250 °/s */
#define MPU6050_GYRO_FS_500         0x01    /* ±500 °/s */
#define MPU6050_GYRO_FS_1000        0x02    /* ±1000 °/s */
#define MPU6050_GYRO_FS_2000        0x03    /* ±2000 °/s */

/* ACCEL_CONFIG */
#define MPU6050_ACCEL_FS_SEL_SHIFT  3
#define MPU6050_ACCEL_FS_2G         0x00    /* ±2g */
#define MPU6050_ACCEL_FS_4G         0x01    /* ±4g */
#define MPU6050_ACCEL_FS_8G         0x02    /* ±8g */
#define MPU6050_ACCEL_FS_16G        0x03    /* ±16g */

/* INT_ENABLE / INT_STATUS */
#define MPU6050_INT_DATA_RDY_EN     BIT(0)
#define MPU6050_INT_FIFO_OFLOW_EN   BIT(4)

/* INT_PIN_CFG */
#define MPU6050_INT_LEVEL_HIGH      0
#define MPU6050_INT_LEVEL_LOW       BIT(7)
#define MPU6050_INT_OPEN_DRAIN      BIT(6)
#define MPU6050_INT_LATCH_EN        BIT(5)
#define MPU6050_INT_RD_CLEAR        BIT(4)

/* WHO_AM_I values */
#define MPU6050_WHO_AM_I_VALUE      0x68
#define MPU6500_WHO_AM_I_VALUE      0x70
#define MPU9250_WHO_AM_I_VALUE      0x71

/*
 * ============================================================
 * Kconfig-dependent Buffer Sizes
 * ============================================================
 */

#ifdef CONFIG_MPU6050_DMA_BUFFER_SIZE
#define MPU6050_DMA_SAMPLES         CONFIG_MPU6050_DMA_BUFFER_SIZE
#else
#define MPU6050_DMA_SAMPLES         64
#endif

#define MPU6050_SAMPLE_SIZE         14  /* 3 accel + 1 temp + 3 gyro × 2 bytes */
#define MPU6050_DMA_BUF_SIZE        (MPU6050_DMA_SAMPLES * MPU6050_SAMPLE_SIZE)

/*
 * ============================================================
 * Scale Values
 * ============================================================
 */

/* Scale values for IIO (micro-units per LSB) */
static const int mpu6050_accel_scale_table[] = {
    598,    /* 2g:  9.80665 / 16384 * 1000000 = 598.550 */
    1197,   /* 4g:  9.80665 / 8192 * 1000000 = 1197.10 */
    2394,   /* 8g:  9.80665 / 4096 * 1000000 = 2394.20 */
    4788,   /* 16g: 9.80665 / 2048 * 1000000 = 4788.40 */
};

static const int mpu6050_gyro_scale_table[] = {
    133,    /* 250:  (1/131) * (π/180) * 1000000 = 133.23 */
    266,    /* 500:  (1/65.5) * (π/180) * 1000000 = 266.32 */
    532,    /* 1000: (1/32.8) * (π/180) * 1000000 = 532.33 */
    1065,   /* 2000: (1/16.4) * (π/180) * 1000000 = 1064.22 */
};

/*
 * ============================================================
 * Device Structure
 * ============================================================
 */

struct mpu6050_data {
    struct i2c_client *client;
    struct regmap *regmap;

    /* Current configuration */
    u8 accel_fs;        /* Accelerometer full-scale range index */
    u8 gyro_fs;         /* Gyroscope full-scale range index */
    u16 sample_rate;    /* Sample rate in Hz */

    /* Calibration offsets */
    s16 accel_offset[3];
    s16 gyro_offset[3];

    /* Data buffer for triggered reads - DMA aligned if DMA enabled */
#ifdef CONFIG_MPU6050_DMA
    u8 *dma_buffer;
    dma_addr_t dma_handle;
    size_t dma_buf_size;
#endif

    /* Standard buffer for non-DMA mode */
    struct {
        s16 accel[3];
        s16 gyro[3];
        s16 temp;
        s64 timestamp __aligned(8);
    } buffer;

#ifdef CONFIG_MPU6050_IRQ
    /* IRQ */
    int irq;
    bool irq_enabled;
#endif

#ifdef CONFIG_MPU6050_TRIGGER
    struct iio_trigger *trig;
#endif

#ifdef CONFIG_MPU6050_FIFO
    /* FIFO state */
    bool fifo_enabled;
    u16 fifo_count;
#endif
};

/*
 * ============================================================
 * Regmap Configuration
 * ============================================================
 */

static bool mpu6050_volatile_reg(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case MPU6050_REG_INT_STATUS:
    case MPU6050_REG_ACCEL_XOUT_H ... MPU6050_REG_GYRO_ZOUT_L:
    case MPU6050_REG_FIFO_COUNT_H:
    case MPU6050_REG_FIFO_COUNT_L:
    case MPU6050_REG_FIFO_R_W:
        return true;
    default:
        return false;
    }
}

static const struct regmap_config mpu6050_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .max_register = 0x75,
    .volatile_reg = mpu6050_volatile_reg,
    .cache_type = REGCACHE_RBTREE,
};

/*
 * ============================================================
 * IIO Channel Definitions
 * ============================================================
 */

enum mpu6050_scan_index {
    MPU6050_SCAN_ACCEL_X,
    MPU6050_SCAN_ACCEL_Y,
    MPU6050_SCAN_ACCEL_Z,
    MPU6050_SCAN_GYRO_X,
    MPU6050_SCAN_GYRO_Y,
    MPU6050_SCAN_GYRO_Z,
    MPU6050_SCAN_TEMP,
    MPU6050_SCAN_TIMESTAMP,
};

#define MPU6050_ACCEL_CHANNEL(axis, idx)                            \
{                                                                   \
    .type = IIO_ACCEL,                                              \
    .modified = 1,                                                  \
    .channel2 = IIO_MOD_##axis,                                     \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |                  \
                          BIT(IIO_CHAN_INFO_CALIBBIAS),             \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |          \
                                BIT(IIO_CHAN_INFO_SAMP_FREQ),       \
    .scan_index = idx,                                              \
    .scan_type = {                                                  \
        .sign = 's',                                                \
        .realbits = 16,                                             \
        .storagebits = 16,                                          \
        .endianness = IIO_BE,                                       \
    },                                                              \
}

#define MPU6050_GYRO_CHANNEL(axis, idx)                             \
{                                                                   \
    .type = IIO_ANGL_VEL,                                           \
    .modified = 1,                                                  \
    .channel2 = IIO_MOD_##axis,                                     \
    .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |                  \
                          BIT(IIO_CHAN_INFO_CALIBBIAS),             \
    .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |          \
                                BIT(IIO_CHAN_INFO_SAMP_FREQ),       \
    .scan_index = idx,                                              \
    .scan_type = {                                                  \
        .sign = 's',                                                \
        .realbits = 16,                                             \
        .storagebits = 16,                                          \
        .endianness = IIO_BE,                                       \
    },                                                              \
}

static const struct iio_chan_spec mpu6050_channels[] = {
    MPU6050_ACCEL_CHANNEL(X, MPU6050_SCAN_ACCEL_X),
    MPU6050_ACCEL_CHANNEL(Y, MPU6050_SCAN_ACCEL_Y),
    MPU6050_ACCEL_CHANNEL(Z, MPU6050_SCAN_ACCEL_Z),
    MPU6050_GYRO_CHANNEL(X, MPU6050_SCAN_GYRO_X),
    MPU6050_GYRO_CHANNEL(Y, MPU6050_SCAN_GYRO_Y),
    MPU6050_GYRO_CHANNEL(Z, MPU6050_SCAN_GYRO_Z),
    {
        .type = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE) |
                              BIT(IIO_CHAN_INFO_OFFSET),
        .scan_index = MPU6050_SCAN_TEMP,
        .scan_type = {
            .sign = 's',
            .realbits = 16,
            .storagebits = 16,
            .endianness = IIO_BE,
        },
    },
    IIO_CHAN_SOFT_TIMESTAMP(MPU6050_SCAN_TIMESTAMP),
};

/*
 * ============================================================
 * Hardware Access Functions
 * ============================================================
 */

static int mpu6050_read_raw_data(struct mpu6050_data *data, int reg, s16 *val)
{
    u8 buf[2];
    int ret;

    ret = regmap_bulk_read(data->regmap, reg, buf, 2);
    if (ret) {
        mpu_dbg(&data->client->dev, "Read reg 0x%02x failed: %d\n", reg, ret);
        return ret;
    }

    *val = (s16)((buf[0] << 8) | buf[1]);
    mpu_dbg(&data->client->dev, "Read reg 0x%02x: raw=%d\n", reg, *val);
    return 0;
}

static int mpu6050_read_accel(struct mpu6050_data *data, int axis, int *val)
{
    s16 raw;
    int ret;

    ret = mpu6050_read_raw_data(data,
                                MPU6050_REG_ACCEL_XOUT_H + (axis * 2),
                                &raw);
    if (ret)
        return ret;

    *val = raw - data->accel_offset[axis];
    return 0;
}

static int mpu6050_read_gyro(struct mpu6050_data *data, int axis, int *val)
{
    s16 raw;
    int ret;

    ret = mpu6050_read_raw_data(data,
                                MPU6050_REG_GYRO_XOUT_H + (axis * 2),
                                &raw);
    if (ret)
        return ret;

    *val = raw - data->gyro_offset[axis];
    return 0;
}

static int mpu6050_read_temp(struct mpu6050_data *data, int *val)
{
    s16 raw;
    int ret;

    ret = mpu6050_read_raw_data(data, MPU6050_REG_TEMP_OUT_H, &raw);
    if (ret)
        return ret;

    *val = raw;
    return 0;
}

static int mpu6050_read_all_data(struct mpu6050_data *data)
{
    u8 buf[14];
    int ret;

    mpu_dbg(&data->client->dev, "Reading all sensor data\n");

    /* Read all sensor data in one burst (accel, temp, gyro) */
    ret = regmap_bulk_read(data->regmap, MPU6050_REG_ACCEL_XOUT_H, buf, 14);
    if (ret)
        return ret;

    /* Parse accelerometer data */
    data->buffer.accel[0] = (s16)((buf[0] << 8) | buf[1]);
    data->buffer.accel[1] = (s16)((buf[2] << 8) | buf[3]);
    data->buffer.accel[2] = (s16)((buf[4] << 8) | buf[5]);

    /* Parse temperature */
    data->buffer.temp = (s16)((buf[6] << 8) | buf[7]);

    /* Parse gyroscope data */
    data->buffer.gyro[0] = (s16)((buf[8] << 8) | buf[9]);
    data->buffer.gyro[1] = (s16)((buf[10] << 8) | buf[11]);
    data->buffer.gyro[2] = (s16)((buf[12] << 8) | buf[13]);

    return 0;
}

/*
 * ============================================================
 * FIFO Functions (CONFIG_MPU6050_FIFO)
 * ============================================================
 */

#ifdef CONFIG_MPU6050_FIFO

static int mpu6050_fifo_enable(struct mpu6050_data *data, bool enable)
{
    int ret;

    mpu_dbg(&data->client->dev, "FIFO %s\n", enable ? "enable" : "disable");

    if (enable) {
        /* Reset FIFO */
        ret = regmap_update_bits(data->regmap, MPU6050_REG_USER_CTRL,
                                 MPU6050_USR_FIFO_RST, MPU6050_USR_FIFO_RST);
        if (ret)
            return ret;

        /* Enable sensors in FIFO */
        ret = regmap_write(data->regmap, MPU6050_REG_FIFO_EN,
                          MPU6050_FIFO_ACCEL_EN |
                          MPU6050_FIFO_TEMP_EN |
                          MPU6050_FIFO_XG_EN |
                          MPU6050_FIFO_YG_EN |
                          MPU6050_FIFO_ZG_EN);
        if (ret)
            return ret;

        /* Enable FIFO */
        ret = regmap_update_bits(data->regmap, MPU6050_REG_USER_CTRL,
                                 MPU6050_USR_FIFO_EN, MPU6050_USR_FIFO_EN);
    } else {
        /* Disable FIFO */
        ret = regmap_update_bits(data->regmap, MPU6050_REG_USER_CTRL,
                                 MPU6050_USR_FIFO_EN, 0);
        if (ret)
            return ret;

        ret = regmap_write(data->regmap, MPU6050_REG_FIFO_EN, 0);
    }

    if (!ret)
        data->fifo_enabled = enable;

    return ret;
}

static int mpu6050_fifo_read_count(struct mpu6050_data *data, u16 *count)
{
    u8 buf[2];
    int ret;

    ret = regmap_bulk_read(data->regmap, MPU6050_REG_FIFO_COUNT_H, buf, 2);
    if (ret)
        return ret;

    *count = (buf[0] << 8) | buf[1];
    mpu_dbg(&data->client->dev, "FIFO count: %u bytes\n", *count);
    return 0;
}

static int mpu6050_fifo_read_data(struct mpu6050_data *data, u8 *buf, size_t len)
{
    return regmap_bulk_read(data->regmap, MPU6050_REG_FIFO_R_W, buf, len);
}

#endif /* CONFIG_MPU6050_FIFO */

/*
 * ============================================================
 * DMA Functions (CONFIG_MPU6050_DMA)
 * ============================================================
 */

#ifdef CONFIG_MPU6050_DMA

static int mpu6050_dma_alloc(struct mpu6050_data *data)
{
    struct device *dev = &data->client->dev;

    data->dma_buf_size = MPU6050_DMA_BUF_SIZE;
    data->dma_buffer = dma_alloc_coherent(dev, data->dma_buf_size,
                                          &data->dma_handle, GFP_KERNEL);
    if (!data->dma_buffer) {
        dev_warn(dev, "DMA buffer allocation failed, using PIO\n");
        return -ENOMEM;
    }

    dev_info(dev, "DMA buffer allocated: %zu bytes\n", data->dma_buf_size);
    return 0;
}

static void mpu6050_dma_free(struct mpu6050_data *data)
{
    struct device *dev = &data->client->dev;

    if (data->dma_buffer) {
        dma_free_coherent(dev, data->dma_buf_size,
                          data->dma_buffer, data->dma_handle);
        data->dma_buffer = NULL;
    }
}

#endif /* CONFIG_MPU6050_DMA */

/*
 * ============================================================
 * Configuration Functions
 * ============================================================
 */

static int mpu6050_set_accel_scale(struct mpu6050_data *data, int fs)
{
    int ret;

    if (fs < 0 || fs > 3)
        return -EINVAL;

    mpu_dbg(&data->client->dev, "Set accel scale: %d\n", fs);

    ret = regmap_update_bits(data->regmap, MPU6050_REG_ACCEL_CONFIG,
                             0x18, fs << MPU6050_ACCEL_FS_SEL_SHIFT);
    if (ret)
        return ret;

    data->accel_fs = fs;
    return 0;
}

static int mpu6050_set_gyro_scale(struct mpu6050_data *data, int fs)
{
    int ret;

    if (fs < 0 || fs > 3)
        return -EINVAL;

    mpu_dbg(&data->client->dev, "Set gyro scale: %d\n", fs);

    ret = regmap_update_bits(data->regmap, MPU6050_REG_GYRO_CONFIG,
                             0x18, fs << MPU6050_GYRO_FS_SEL_SHIFT);
    if (ret)
        return ret;

    data->gyro_fs = fs;
    return 0;
}

static int mpu6050_set_sample_rate(struct mpu6050_data *data, int rate)
{
    u8 divider;

    if (rate < 4 || rate > 1000)
        return -EINVAL;

    divider = (1000 / rate) - 1;
    data->sample_rate = 1000 / (divider + 1);

    mpu_dbg(&data->client->dev, "Set sample rate: %d Hz (div=%d)\n",
            data->sample_rate, divider);

    return regmap_write(data->regmap, MPU6050_REG_SMPLRT_DIV, divider);
}

/*
 * ============================================================
 * IIO Read/Write Callbacks
 * ============================================================
 */

static int mpu6050_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;
    int axis;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        ret = iio_device_claim_direct_mode(indio_dev);
        if (ret)
            return ret;

        switch (chan->type) {
        case IIO_ACCEL:
            axis = chan->channel2 - IIO_MOD_X;
            ret = mpu6050_read_accel(data, axis, val);
            break;
        case IIO_ANGL_VEL:
            axis = chan->channel2 - IIO_MOD_X;
            ret = mpu6050_read_gyro(data, axis, val);
            break;
        case IIO_TEMP:
            ret = mpu6050_read_temp(data, val);
            break;
        default:
            ret = -EINVAL;
        }

        iio_device_release_direct_mode(indio_dev);

        if (ret)
            return ret;
        return IIO_VAL_INT;

    case IIO_CHAN_INFO_SCALE:
        switch (chan->type) {
        case IIO_ACCEL:
            *val = 0;
            *val2 = mpu6050_accel_scale_table[data->accel_fs];
            return IIO_VAL_INT_PLUS_MICRO;
        case IIO_ANGL_VEL:
            *val = 0;
            *val2 = mpu6050_gyro_scale_table[data->gyro_fs];
            return IIO_VAL_INT_PLUS_MICRO;
        case IIO_TEMP:
            *val = 0;
            *val2 = 2941;
            return IIO_VAL_INT_PLUS_MICRO;
        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_OFFSET:
        if (chan->type == IIO_TEMP) {
            *val = 12420;
            return IIO_VAL_INT;
        }
        return -EINVAL;

    case IIO_CHAN_INFO_CALIBBIAS:
        switch (chan->type) {
        case IIO_ACCEL:
            axis = chan->channel2 - IIO_MOD_X;
            *val = data->accel_offset[axis];
            return IIO_VAL_INT;
        case IIO_ANGL_VEL:
            axis = chan->channel2 - IIO_MOD_X;
            *val = data->gyro_offset[axis];
            return IIO_VAL_INT;
        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_SAMP_FREQ:
        *val = data->sample_rate;
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

static int mpu6050_write_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int val, int val2, long mask)
{
    struct mpu6050_data *data = iio_priv(indio_dev);
    int axis;
    int i;

    switch (mask) {
    case IIO_CHAN_INFO_SCALE:
        switch (chan->type) {
        case IIO_ACCEL:
            for (i = 0; i < 4; i++) {
                if (val2 == mpu6050_accel_scale_table[i])
                    return mpu6050_set_accel_scale(data, i);
            }
            return -EINVAL;
        case IIO_ANGL_VEL:
            for (i = 0; i < 4; i++) {
                if (val2 == mpu6050_gyro_scale_table[i])
                    return mpu6050_set_gyro_scale(data, i);
            }
            return -EINVAL;
        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_CALIBBIAS:
        switch (chan->type) {
        case IIO_ACCEL:
            axis = chan->channel2 - IIO_MOD_X;
            data->accel_offset[axis] = val;
            return 0;
        case IIO_ANGL_VEL:
            axis = chan->channel2 - IIO_MOD_X;
            data->gyro_offset[axis] = val;
            return 0;
        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_SAMP_FREQ:
        return mpu6050_set_sample_rate(data, val);

    default:
        return -EINVAL;
    }
}

static int mpu6050_write_raw_get_fmt(struct iio_dev *indio_dev,
                                     struct iio_chan_spec const *chan,
                                     long mask)
{
    switch (mask) {
    case IIO_CHAN_INFO_SCALE:
        return IIO_VAL_INT_PLUS_MICRO;
    case IIO_CHAN_INFO_CALIBBIAS:
    case IIO_CHAN_INFO_SAMP_FREQ:
        return IIO_VAL_INT;
    default:
        return -EINVAL;
    }
}

/*
 * ============================================================
 * Triggered Buffer (CONFIG_MPU6050_TRIGGER)
 * ============================================================
 */

#ifdef CONFIG_MPU6050_TRIGGER

static irqreturn_t mpu6050_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    mpu_dbg(&data->client->dev, "Trigger handler called\n");

#ifdef CONFIG_MPU6050_FIFO
    if (data->fifo_enabled) {
        u16 count;
        ret = mpu6050_fifo_read_count(data, &count);
        if (!ret && count >= MPU6050_SAMPLE_SIZE) {
            u8 fifo_buf[MPU6050_SAMPLE_SIZE];
            ret = mpu6050_fifo_read_data(data, fifo_buf, MPU6050_SAMPLE_SIZE);
            if (!ret) {
                /* Parse FIFO data */
                data->buffer.accel[0] = (s16)((fifo_buf[0] << 8) | fifo_buf[1]);
                data->buffer.accel[1] = (s16)((fifo_buf[2] << 8) | fifo_buf[3]);
                data->buffer.accel[2] = (s16)((fifo_buf[4] << 8) | fifo_buf[5]);
                data->buffer.temp = (s16)((fifo_buf[6] << 8) | fifo_buf[7]);
                data->buffer.gyro[0] = (s16)((fifo_buf[8] << 8) | fifo_buf[9]);
                data->buffer.gyro[1] = (s16)((fifo_buf[10] << 8) | fifo_buf[11]);
                data->buffer.gyro[2] = (s16)((fifo_buf[12] << 8) | fifo_buf[13]);
            }
        }
    } else
#endif
    {
        ret = mpu6050_read_all_data(data);
    }

    if (!ret)
        iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
                                           iio_get_time_ns(indio_dev));

    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}

#endif /* CONFIG_MPU6050_TRIGGER */

/*
 * ============================================================
 * Data Ready Interrupt (CONFIG_MPU6050_IRQ)
 * ============================================================
 */

#ifdef CONFIG_MPU6050_IRQ

static irqreturn_t mpu6050_data_ready_irq(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
#ifdef CONFIG_MPU6050_TRIGGER
    struct mpu6050_data *data = iio_priv(indio_dev);

    mpu_dbg(&data->client->dev, "Data ready IRQ\n");

    if (data->trig)
        iio_trigger_poll(data->trig);
#endif

    return IRQ_HANDLED;
}

#ifdef CONFIG_MPU6050_TRIGGER
static int mpu6050_trigger_set_state(struct iio_trigger *trig, bool state)
{
    struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    mpu_dbg(&data->client->dev, "Trigger state: %s\n",
            state ? "enable" : "disable");

    if (state) {
        /* Enable data ready interrupt */
        ret = regmap_write(data->regmap, MPU6050_REG_INT_ENABLE,
                           MPU6050_INT_DATA_RDY_EN);

#ifdef CONFIG_MPU6050_FIFO
        /* Enable FIFO if configured */
        if (!ret)
            ret = mpu6050_fifo_enable(data, true);
#endif
    } else {
        /* Disable interrupts */
        ret = regmap_write(data->regmap, MPU6050_REG_INT_ENABLE, 0);

#ifdef CONFIG_MPU6050_FIFO
        mpu6050_fifo_enable(data, false);
#endif
    }

    if (!ret)
        data->irq_enabled = state;

    return ret;
}

static const struct iio_trigger_ops mpu6050_trigger_ops = {
    .set_trigger_state = mpu6050_trigger_set_state,
};
#endif /* CONFIG_MPU6050_TRIGGER */

#endif /* CONFIG_MPU6050_IRQ */

/*
 * ============================================================
 * IIO Info Structure
 * ============================================================
 */

static const struct iio_info mpu6050_info = {
    .read_raw = mpu6050_read_raw,
    .write_raw = mpu6050_write_raw,
    .write_raw_get_fmt = mpu6050_write_raw_get_fmt,
};

/*
 * ============================================================
 * Hardware Initialization
 * ============================================================
 */

static int mpu6050_hw_init(struct mpu6050_data *data)
{
    unsigned int val;
    int ret;

    mpu_dbg(&data->client->dev, "Hardware init starting\n");

    /* Reset device */
    ret = regmap_write(data->regmap, MPU6050_REG_PWR_MGMT_1,
                       MPU6050_PWR1_DEVICE_RESET);
    if (ret)
        return ret;

    /* Wait for reset to complete */
    msleep(100);

    /* Wake up and select PLL clock source */
    ret = regmap_write(data->regmap, MPU6050_REG_PWR_MGMT_1,
                       MPU6050_PWR1_CLKSEL_PLL_X);
    if (ret)
        return ret;

    msleep(10);

    /* Verify device ID */
    ret = regmap_read(data->regmap, MPU6050_REG_WHO_AM_I, &val);
    if (ret)
        return ret;

    if (val != MPU6050_WHO_AM_I_VALUE &&
        val != MPU6500_WHO_AM_I_VALUE &&
        val != MPU9250_WHO_AM_I_VALUE) {
        dev_err(&data->client->dev, "Unknown device ID: 0x%02x\n", val);
        return -ENODEV;
    }

    dev_info(&data->client->dev, "MPU device ID: 0x%02x\n", val);

    /* Configure default settings */
    ret = regmap_write(data->regmap, MPU6050_REG_CONFIG, 0x03);
    if (ret)
        return ret;

    ret = mpu6050_set_accel_scale(data, MPU6050_ACCEL_FS_2G);
    if (ret)
        return ret;

    ret = mpu6050_set_gyro_scale(data, MPU6050_GYRO_FS_250);
    if (ret)
        return ret;

    ret = mpu6050_set_sample_rate(data, 100);
    if (ret)
        return ret;

#ifdef CONFIG_MPU6050_IRQ
    /* Configure interrupt pin */
    ret = regmap_write(data->regmap, MPU6050_REG_INT_PIN_CFG,
                       MPU6050_INT_LATCH_EN | MPU6050_INT_RD_CLEAR);
    if (ret)
        return ret;
#endif

    mpu_dbg(&data->client->dev, "Hardware init complete\n");
    return 0;
}

/*
 * ============================================================
 * Probe and Remove
 * ============================================================
 */

static int mpu6050_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct iio_dev *indio_dev;
    struct mpu6050_data *data;
    int ret;

    dev_info(dev, "MPU6050 probe starting\n");

    /* Print enabled features */
    dev_info(dev, "Features: %s%s%s%s%s\n",
#ifdef CONFIG_MPU6050_IRQ
             "IRQ ",
#else
             "",
#endif
#ifdef CONFIG_MPU6050_TRIGGER
             "TRIGGER ",
#else
             "",
#endif
#ifdef CONFIG_MPU6050_DMA
             "DMA ",
#else
             "",
#endif
#ifdef CONFIG_MPU6050_FIFO
             "FIFO ",
#else
             "",
#endif
#ifdef CONFIG_MPU6050_DEBUG
             "DEBUG"
#else
             ""
#endif
    );

    /* Allocate IIO device */
    indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->client = client;

    i2c_set_clientdata(client, indio_dev);

    /* Initialize regmap */
    data->regmap = devm_regmap_init_i2c(client, &mpu6050_regmap_config);
    if (IS_ERR(data->regmap)) {
        dev_err(dev, "Failed to init regmap\n");
        return PTR_ERR(data->regmap);
    }

#ifdef CONFIG_MPU6050_DMA
    /* Allocate DMA buffer */
    ret = mpu6050_dma_alloc(data);
    if (ret)
        dev_warn(dev, "DMA not available, using PIO\n");
#endif

    /* Initialize hardware */
    ret = mpu6050_hw_init(data);
    if (ret) {
        dev_err(dev, "Hardware init failed\n");
        goto err_dma_free;
    }

    /* Setup IIO device */
    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &mpu6050_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = mpu6050_channels;
    indio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);

#ifdef CONFIG_MPU6050_TRIGGER
    /* Setup triggered buffer */
    ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
                                          iio_pollfunc_store_time,
                                          mpu6050_trigger_handler,
                                          NULL);
    if (ret) {
        dev_err(dev, "Failed to setup triggered buffer\n");
        goto err_dma_free;
    }
#endif

#ifdef CONFIG_MPU6050_IRQ
    /* Setup data ready trigger if IRQ available */
    data->irq = client->irq;
    if (data->irq > 0) {
        dev_info(dev, "Setting up IRQ %d\n", data->irq);

#ifdef CONFIG_MPU6050_TRIGGER
        data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
                                            indio_dev->name,
                                            iio_device_id(indio_dev));
        if (!data->trig) {
            ret = -ENOMEM;
            goto err_dma_free;
        }

        data->trig->ops = &mpu6050_trigger_ops;
        iio_trigger_set_drvdata(data->trig, indio_dev);

        ret = devm_iio_trigger_register(dev, data->trig);
        if (ret) {
            dev_err(dev, "Failed to register trigger\n");
            goto err_dma_free;
        }
#endif

        ret = devm_request_irq(dev, data->irq, mpu6050_data_ready_irq,
                               IRQF_TRIGGER_RISING,
                               DRIVER_NAME, indio_dev);
        if (ret) {
            dev_err(dev, "Failed to request IRQ\n");
            goto err_dma_free;
        }

#ifdef CONFIG_MPU6050_TRIGGER
        indio_dev->trig = iio_trigger_get(data->trig);
#endif
    } else {
        dev_info(dev, "No IRQ specified, polling mode only\n");
    }
#endif /* CONFIG_MPU6050_IRQ */

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "Failed to register IIO device\n");
        goto err_dma_free;
    }

    dev_info(dev, "MPU6050 initialized, sample rate %d Hz\n",
             data->sample_rate);

    return 0;

err_dma_free:
#ifdef CONFIG_MPU6050_DMA
    mpu6050_dma_free(data);
#endif
    return ret;
}

static void mpu6050_remove(struct i2c_client *client)
{
    struct iio_dev *indio_dev = i2c_get_clientdata(client);
    struct mpu6050_data *data = iio_priv(indio_dev);

    mpu_dbg(&client->dev, "Removing driver\n");

#ifdef CONFIG_MPU6050_FIFO
    mpu6050_fifo_enable(data, false);
#endif

#ifdef CONFIG_MPU6050_DMA
    mpu6050_dma_free(data);
#endif

    /* Put device to sleep */
    regmap_update_bits(data->regmap, MPU6050_REG_PWR_MGMT_1,
                       MPU6050_PWR1_SLEEP, MPU6050_PWR1_SLEEP);
}

/*
 * ============================================================
 * Power Management
 * ============================================================
 */

static int mpu6050_suspend(struct device *dev)
{
    struct iio_dev *indio_dev = dev_get_drvdata(dev);
    struct mpu6050_data *data = iio_priv(indio_dev);

    mpu_dbg(dev, "Suspending\n");

    return regmap_update_bits(data->regmap, MPU6050_REG_PWR_MGMT_1,
                              MPU6050_PWR1_SLEEP, MPU6050_PWR1_SLEEP);
}

static int mpu6050_resume(struct device *dev)
{
    struct iio_dev *indio_dev = dev_get_drvdata(dev);
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    mpu_dbg(dev, "Resuming\n");

    ret = regmap_update_bits(data->regmap, MPU6050_REG_PWR_MGMT_1,
                             MPU6050_PWR1_SLEEP, 0);
    if (ret)
        return ret;

    msleep(10);
    return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(mpu6050_pm_ops,
                                mpu6050_suspend, mpu6050_resume);

/*
 * ============================================================
 * Driver Registration
 * ============================================================
 */

static const struct of_device_id mpu6050_of_match[] = {
    { .compatible = "invensense,mpu6050" },
    { .compatible = "invensense,mpu6500" },
    { .compatible = "invensense,mpu9250" },
    { }
};
MODULE_DEVICE_TABLE(of, mpu6050_of_match);

static const struct i2c_device_id mpu6050_id[] = {
    { "mpu6050", 0 },
    { "mpu6500", 0 },
    { "mpu9250", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, mpu6050_id);

static struct i2c_driver mpu6050_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = mpu6050_of_match,
        .pm = pm_sleep_ptr(&mpu6050_pm_ops),
    },
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
    .id_table = mpu6050_id,
};
module_i2c_driver(mpu6050_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("InvenSense MPU6050 6-Axis IMU Driver with Kconfig options");
MODULE_VERSION("2.0");
