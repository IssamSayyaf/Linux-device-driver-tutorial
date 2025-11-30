/*
 * mpu6050.c - InvenSense MPU6050 6-Axis Accelerometer/Gyroscope Driver
 *
 * This driver demonstrates:
 * - I2C client driver
 * - IIO subsystem integration
 * - Triggered buffer support
 * - Interrupt handling
 * - Power management
 * - Device tree configuration
 *
 * The MPU6050 is a 6-axis motion tracking device combining:
 * - 3-axis gyroscope (±250/500/1000/2000 °/s)
 * - 3-axis accelerometer (±2/4/8/16 g)
 * - Temperature sensor
 * - Digital Motion Processor (DMP)
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#define DRIVER_NAME "mpu6050"

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

/* Power management */
#define MPU6050_REG_USER_CTRL       0x6A    /* User Control */
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

/* INT_PIN_CFG */
#define MPU6050_INT_LEVEL_HIGH      0
#define MPU6050_INT_LEVEL_LOW       BIT(7)
#define MPU6050_INT_OPEN_DRAIN      BIT(6)
#define MPU6050_INT_LATCH_EN        BIT(5)
#define MPU6050_INT_RD_CLEAR        BIT(4)

/* WHO_AM_I value */
#define MPU6050_WHO_AM_I_VALUE      0x68
#define MPU6500_WHO_AM_I_VALUE      0x70
#define MPU9250_WHO_AM_I_VALUE      0x71

/*
 * ============================================================
 * Scale Values
 * ============================================================
 */

/* Accelerometer scale: LSB/g */
static const int mpu6050_accel_scale[] = {
    16384,  /* ±2g:  16384 LSB/g */
    8192,   /* ±4g:  8192 LSB/g */
    4096,   /* ±8g:  4096 LSB/g */
    2048,   /* ±16g: 2048 LSB/g */
};

/* Gyroscope scale: LSB/(°/s) */
static const int mpu6050_gyro_scale[] = {
    131,    /* ±250 °/s:  131 LSB/(°/s) */
    65,     /* ±500 °/s:  65.5 LSB/(°/s) */
    32,     /* ±1000 °/s: 32.8 LSB/(°/s) */
    16,     /* ±2000 °/s: 16.4 LSB/(°/s) */
};

/* Scale values for IIO (micro-units per LSB) */
/* Accel: m/s² = raw / scale * 9.80665 */
static const int mpu6050_accel_scale_table[] = {
    598,    /* 2g:  9.80665 / 16384 * 1000000 = 598.550 */
    1197,   /* 4g:  9.80665 / 8192 * 1000000 = 1197.10 */
    2394,   /* 8g:  9.80665 / 4096 * 1000000 = 2394.20 */
    4788,   /* 16g: 9.80665 / 2048 * 1000000 = 4788.40 */
};

/* Gyro: rad/s = raw / scale * (π/180) */
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
    struct iio_trigger *trig;

    /* Current configuration */
    u8 accel_fs;        /* Accelerometer full-scale range index */
    u8 gyro_fs;         /* Gyroscope full-scale range index */
    u16 sample_rate;    /* Sample rate in Hz */

    /* Calibration offsets */
    s16 accel_offset[3];
    s16 gyro_offset[3];

    /* Data buffer for triggered reads */
    struct {
        s16 accel[3];
        s16 gyro[3];
        s16 temp;
        s64 timestamp __aligned(8);
    } buffer;

    /* IRQ */
    int irq;
    bool irq_enabled;
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
    if (ret)
        return ret;

    *val = (s16)((buf[0] << 8) | buf[1]);
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
 * Configuration Functions
 * ============================================================
 */

static int mpu6050_set_accel_scale(struct mpu6050_data *data, int fs)
{
    int ret;

    if (fs < 0 || fs > 3)
        return -EINVAL;

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

    /* Sample Rate = Gyro Output Rate / (1 + SMPLRT_DIV)
     * Gyro Output Rate = 1kHz when DLPF is enabled
     */
    divider = (1000 / rate) - 1;

    data->sample_rate = 1000 / (divider + 1);

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
            /* Return scale in m/s² per LSB */
            *val = 0;
            *val2 = mpu6050_accel_scale_table[data->accel_fs];
            return IIO_VAL_INT_PLUS_MICRO;
        case IIO_ANGL_VEL:
            /* Return scale in rad/s per LSB */
            *val = 0;
            *val2 = mpu6050_gyro_scale_table[data->gyro_fs];
            return IIO_VAL_INT_PLUS_MICRO;
        case IIO_TEMP:
            /* Scale: 1/340 °C per LSB = 2941.18 micro */
            *val = 0;
            *val2 = 2941;
            return IIO_VAL_INT_PLUS_MICRO;
        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_OFFSET:
        if (chan->type == IIO_TEMP) {
            /* Offset: 36.53°C at raw=0, so offset = 36.53 * 340 = 12420 */
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
 * Triggered Buffer
 * ============================================================
 */

static irqreturn_t mpu6050_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    ret = mpu6050_read_all_data(data);
    if (ret)
        goto done;

    iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
                                       iio_get_time_ns(indio_dev));

done:
    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}

/*
 * ============================================================
 * Data Ready Interrupt
 * ============================================================
 */

static irqreturn_t mpu6050_data_ready_irq(int irq, void *private)
{
    struct iio_dev *indio_dev = private;
    struct mpu6050_data *data = iio_priv(indio_dev);

    if (data->trig)
        iio_trigger_poll(data->trig);

    return IRQ_HANDLED;
}

/*
 * ============================================================
 * Trigger Operations
 * ============================================================
 */

static int mpu6050_trigger_set_state(struct iio_trigger *trig, bool state)
{
    struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    if (state) {
        /* Enable data ready interrupt */
        ret = regmap_write(data->regmap, MPU6050_REG_INT_ENABLE,
                           MPU6050_INT_DATA_RDY_EN);
    } else {
        /* Disable data ready interrupt */
        ret = regmap_write(data->regmap, MPU6050_REG_INT_ENABLE, 0);
    }

    if (ret)
        return ret;

    data->irq_enabled = state;
    return 0;
}

static const struct iio_trigger_ops mpu6050_trigger_ops = {
    .set_trigger_state = mpu6050_trigger_set_state,
};

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
    /* Set DLPF to 42 Hz for both accel and gyro */
    ret = regmap_write(data->regmap, MPU6050_REG_CONFIG, 0x03);
    if (ret)
        return ret;

    /* Set accelerometer to ±2g */
    ret = mpu6050_set_accel_scale(data, MPU6050_ACCEL_FS_2G);
    if (ret)
        return ret;

    /* Set gyroscope to ±250 °/s */
    ret = mpu6050_set_gyro_scale(data, MPU6050_GYRO_FS_250);
    if (ret)
        return ret;

    /* Set sample rate to 100 Hz */
    ret = mpu6050_set_sample_rate(data, 100);
    if (ret)
        return ret;

    /* Configure interrupt pin */
    ret = regmap_write(data->regmap, MPU6050_REG_INT_PIN_CFG,
                       MPU6050_INT_LATCH_EN | MPU6050_INT_RD_CLEAR);
    if (ret)
        return ret;

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

    /* Initialize hardware */
    ret = mpu6050_hw_init(data);
    if (ret) {
        dev_err(dev, "Hardware init failed\n");
        return ret;
    }

    /* Setup IIO device */
    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &mpu6050_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = mpu6050_channels;
    indio_dev->num_channels = ARRAY_SIZE(mpu6050_channels);

    /* Setup triggered buffer */
    ret = devm_iio_triggered_buffer_setup(dev, indio_dev,
                                          iio_pollfunc_store_time,
                                          mpu6050_trigger_handler,
                                          NULL);
    if (ret) {
        dev_err(dev, "Failed to setup triggered buffer\n");
        return ret;
    }

    /* Setup data ready trigger if IRQ available */
    data->irq = client->irq;
    if (data->irq > 0) {
        data->trig = devm_iio_trigger_alloc(dev, "%s-dev%d",
                                            indio_dev->name,
                                            iio_device_id(indio_dev));
        if (!data->trig)
            return -ENOMEM;

        data->trig->ops = &mpu6050_trigger_ops;
        iio_trigger_set_drvdata(data->trig, indio_dev);

        ret = devm_iio_trigger_register(dev, data->trig);
        if (ret) {
            dev_err(dev, "Failed to register trigger\n");
            return ret;
        }

        ret = devm_request_irq(dev, data->irq, mpu6050_data_ready_irq,
                               IRQF_TRIGGER_RISING,
                               DRIVER_NAME, indio_dev);
        if (ret) {
            dev_err(dev, "Failed to request IRQ\n");
            return ret;
        }

        indio_dev->trig = iio_trigger_get(data->trig);
    }

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "Failed to register IIO device\n");
        return ret;
    }

    dev_info(dev, "MPU6050 initialized, sample rate %d Hz\n",
             data->sample_rate);

    return 0;
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

    /* Put device to sleep */
    return regmap_update_bits(data->regmap, MPU6050_REG_PWR_MGMT_1,
                              MPU6050_PWR1_SLEEP, MPU6050_PWR1_SLEEP);
}

static int mpu6050_resume(struct device *dev)
{
    struct iio_dev *indio_dev = dev_get_drvdata(dev);
    struct mpu6050_data *data = iio_priv(indio_dev);
    int ret;

    /* Wake up device */
    ret = regmap_update_bits(data->regmap, MPU6050_REG_PWR_MGMT_1,
                             MPU6050_PWR1_SLEEP, 0);
    if (ret)
        return ret;

    /* Wait for device to stabilize */
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
    .id_table = mpu6050_id,
};
module_i2c_driver(mpu6050_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("InvenSense MPU6050 6-Axis Accelerometer/Gyroscope Driver");
MODULE_VERSION("1.0");
