/*
 * hc_sr04.c - HC-SR04 Ultrasonic Distance Sensor Driver
 *
 * This driver demonstrates:
 * - GPIO subsystem integration
 * - IIO distance channel
 * - High-resolution timing with ktime
 * - Interrupt-driven measurement
 * - Timeout handling
 *
 * The HC-SR04 works by:
 * 1. Sending a 10µs trigger pulse
 * 2. Measuring the echo pulse width
 * 3. Distance = (echo_time * speed_of_sound) / 2
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/ktime.h>
#include <linux/of.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

#define DRIVER_NAME "hc-sr04"

/* HC-SR04 timing constants */
#define TRIGGER_PULSE_US        10      /* Trigger pulse width */
#define ECHO_TIMEOUT_MS         30      /* Max echo time ~5m */
#define MEASUREMENT_INTERVAL_MS 60      /* Minimum between measurements */

/* Speed of sound at 20°C = 343.2 m/s = 0.03432 cm/µs */
/* Distance (cm) = echo_time_us / 58 */
/* Distance (mm) = echo_time_us / 5.8 = echo_time_us * 10 / 58 */
#define DISTANCE_DIVISOR        58      /* For cm */

/*
 * ============================================================
 * Device Structure
 * ============================================================
 */

struct hc_sr04_data {
    struct device *dev;
    struct gpio_desc *trigger_gpio;
    struct gpio_desc *echo_gpio;
    int echo_irq;

    /* Measurement state */
    struct completion echo_complete;
    ktime_t echo_start;
    ktime_t echo_end;
    bool echo_received;

    /* Configuration */
    int temperature;            /* Temperature for speed correction (milli-°C) */

    /* Statistics */
    unsigned long measurements;
    unsigned long timeouts;
    unsigned long errors;

    /* Mutex for serializing measurements */
    struct mutex lock;
};

/*
 * ============================================================
 * Echo Interrupt Handler
 * ============================================================
 */

static irqreturn_t hc_sr04_echo_irq(int irq, void *dev_id)
{
    struct hc_sr04_data *data = dev_id;
    int echo_level;

    echo_level = gpiod_get_value(data->echo_gpio);

    if (echo_level) {
        /* Rising edge - echo started */
        data->echo_start = ktime_get();
    } else {
        /* Falling edge - echo ended */
        data->echo_end = ktime_get();
        data->echo_received = true;
        complete(&data->echo_complete);
    }

    return IRQ_HANDLED;
}

/*
 * ============================================================
 * Distance Measurement
 * ============================================================
 */

static int hc_sr04_measure_distance(struct hc_sr04_data *data, int *distance_mm)
{
    ktime_t echo_duration;
    s64 echo_us;
    int ret;

    mutex_lock(&data->lock);

    /* Reset state */
    reinit_completion(&data->echo_complete);
    data->echo_received = false;
    data->echo_start = 0;
    data->echo_end = 0;

    /* Generate trigger pulse (10µs high) */
    gpiod_set_value(data->trigger_gpio, 1);
    udelay(TRIGGER_PULSE_US);
    gpiod_set_value(data->trigger_gpio, 0);

    /* Wait for echo with timeout */
    ret = wait_for_completion_timeout(&data->echo_complete,
                                      msecs_to_jiffies(ECHO_TIMEOUT_MS));
    if (!ret) {
        data->timeouts++;
        mutex_unlock(&data->lock);
        return -ETIMEDOUT;
    }

    if (!data->echo_received) {
        data->errors++;
        mutex_unlock(&data->lock);
        return -EIO;
    }

    /* Calculate echo duration */
    echo_duration = ktime_sub(data->echo_end, data->echo_start);
    echo_us = ktime_to_us(echo_duration);

    /* Validate timing */
    if (echo_us < 0 || echo_us > 30000) {
        data->errors++;
        mutex_unlock(&data->lock);
        return -ERANGE;
    }

    /*
     * Calculate distance:
     * Distance = (echo_time_us * speed_of_sound) / 2
     *
     * Speed of sound at temperature T:
     * v = 331.3 + 0.606 * T (m/s)
     *
     * At 20°C: v = 343.4 m/s = 0.03434 cm/µs
     * Distance (mm) = echo_us * 0.1717 ≈ echo_us / 5.825
     *
     * For simplicity: distance_mm = echo_us * 10 / 58
     */
    if (data->temperature != 20000) {
        /* Temperature compensation */
        /* v = 331.3 + 0.606 * (T/1000) m/s */
        int temp_c = data->temperature / 1000;
        int speed = 3313 + 6 * temp_c;  /* speed in 0.1 m/s */
        *distance_mm = (echo_us * speed) / 20000;
    } else {
        /* Standard calculation at 20°C */
        *distance_mm = (int)((echo_us * 10) / 58);
    }

    data->measurements++;
    mutex_unlock(&data->lock);

    return 0;
}

/*
 * ============================================================
 * IIO Channel Definition
 * ============================================================
 */

static const struct iio_chan_spec hc_sr04_channels[] = {
    {
        .type = IIO_DISTANCE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SAMP_FREQ),
    },
    {
        .type = IIO_TEMP,
        .output = 1,    /* This is a configuration input */
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
};

/*
 * ============================================================
 * IIO Read/Write Callbacks
 * ============================================================
 */

static int hc_sr04_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct hc_sr04_data *data = iio_priv(indio_dev);
    int distance_mm;
    int ret;

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        switch (chan->type) {
        case IIO_DISTANCE:
            ret = hc_sr04_measure_distance(data, &distance_mm);
            if (ret)
                return ret;
            *val = distance_mm;
            return IIO_VAL_INT;

        case IIO_TEMP:
            /* Return configured temperature */
            *val = data->temperature;
            return IIO_VAL_INT;

        default:
            return -EINVAL;
        }

    case IIO_CHAN_INFO_SCALE:
        if (chan->type == IIO_DISTANCE) {
            /* Raw value is in mm, scale to m */
            *val = 0;
            *val2 = 1000;   /* 0.001 m per mm */
            return IIO_VAL_INT_PLUS_MICRO;
        }
        return -EINVAL;

    case IIO_CHAN_INFO_SAMP_FREQ:
        /* Maximum ~16 Hz due to echo timeout */
        *val = 1000 / MEASUREMENT_INTERVAL_MS;
        return IIO_VAL_INT;

    default:
        return -EINVAL;
    }
}

static int hc_sr04_write_raw(struct iio_dev *indio_dev,
                             struct iio_chan_spec const *chan,
                             int val, int val2, long mask)
{
    struct hc_sr04_data *data = iio_priv(indio_dev);

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        if (chan->type == IIO_TEMP && chan->output) {
            /* Set temperature for speed of sound correction */
            /* Value in milli-degrees Celsius */
            if (val < -40000 || val > 85000)
                return -EINVAL;
            data->temperature = val;
            return 0;
        }
        return -EINVAL;

    default:
        return -EINVAL;
    }
}

/*
 * ============================================================
 * Sysfs Attributes
 * ============================================================
 */

static ssize_t hc_sr04_show_stats(struct device *dev,
                                  struct device_attribute *attr,
                                  char *buf)
{
    struct iio_dev *indio_dev = dev_to_iio_dev(dev);
    struct hc_sr04_data *data = iio_priv(indio_dev);

    return sprintf(buf, "measurements: %lu\ntimeouts: %lu\nerrors: %lu\n",
                   data->measurements, data->timeouts, data->errors);
}

static IIO_DEVICE_ATTR(statistics, 0444, hc_sr04_show_stats, NULL, 0);

static struct attribute *hc_sr04_attrs[] = {
    &iio_dev_attr_statistics.dev_attr.attr,
    NULL,
};

static const struct attribute_group hc_sr04_attr_group = {
    .attrs = hc_sr04_attrs,
};

/*
 * ============================================================
 * IIO Info Structure
 * ============================================================
 */

static const struct iio_info hc_sr04_info = {
    .read_raw = hc_sr04_read_raw,
    .write_raw = hc_sr04_write_raw,
    .attrs = &hc_sr04_attr_group,
};

/*
 * ============================================================
 * Probe and Remove
 * ============================================================
 */

static int hc_sr04_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct iio_dev *indio_dev;
    struct hc_sr04_data *data;
    int ret;

    /* Allocate IIO device */
    indio_dev = devm_iio_device_alloc(dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->dev = dev;

    platform_set_drvdata(pdev, indio_dev);

    mutex_init(&data->lock);
    init_completion(&data->echo_complete);

    /* Default temperature: 20°C = 20000 milli-°C */
    data->temperature = 20000;

    /* Get trigger GPIO (output) */
    data->trigger_gpio = devm_gpiod_get(dev, "trigger", GPIOD_OUT_LOW);
    if (IS_ERR(data->trigger_gpio)) {
        ret = PTR_ERR(data->trigger_gpio);
        dev_err(dev, "Failed to get trigger GPIO: %d\n", ret);
        return ret;
    }

    /* Get echo GPIO (input) */
    data->echo_gpio = devm_gpiod_get(dev, "echo", GPIOD_IN);
    if (IS_ERR(data->echo_gpio)) {
        ret = PTR_ERR(data->echo_gpio);
        dev_err(dev, "Failed to get echo GPIO: %d\n", ret);
        return ret;
    }

    /* Get IRQ for echo GPIO */
    data->echo_irq = gpiod_to_irq(data->echo_gpio);
    if (data->echo_irq < 0) {
        dev_err(dev, "Failed to get echo IRQ\n");
        return data->echo_irq;
    }

    /* Request IRQ for both edges */
    ret = devm_request_irq(dev, data->echo_irq, hc_sr04_echo_irq,
                           IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           DRIVER_NAME, data);
    if (ret) {
        dev_err(dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    /* Setup IIO device */
    indio_dev->name = DRIVER_NAME;
    indio_dev->info = &hc_sr04_info;
    indio_dev->modes = INDIO_DIRECT_MODE;
    indio_dev->channels = hc_sr04_channels;
    indio_dev->num_channels = ARRAY_SIZE(hc_sr04_channels);

    /* Register IIO device */
    ret = devm_iio_device_register(dev, indio_dev);
    if (ret) {
        dev_err(dev, "Failed to register IIO device: %d\n", ret);
        return ret;
    }

    dev_info(dev, "HC-SR04 ultrasonic sensor initialized\n");
    return 0;
}

/*
 * ============================================================
 * Driver Registration
 * ============================================================
 */

static const struct of_device_id hc_sr04_of_match[] = {
    { .compatible = "hc-sr04" },
    { .compatible = "elecfreaks,hc-sr04" },
    { }
};
MODULE_DEVICE_TABLE(of, hc_sr04_of_match);

static struct platform_driver hc_sr04_driver = {
    .probe = hc_sr04_probe,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = hc_sr04_of_match,
    },
};
module_platform_driver(hc_sr04_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("HC-SR04 Ultrasonic Distance Sensor Driver");
MODULE_VERSION("1.0");
