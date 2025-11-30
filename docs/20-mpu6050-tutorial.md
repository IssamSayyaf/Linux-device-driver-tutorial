# MPU6050 Accelerometer/Gyroscope Driver Tutorial

Complete guide to understanding and building the MPU6050 6-axis IMU driver.

## What This Driver Demonstrates

| Feature | Implementation | Location |
|---------|---------------|----------|
| I2C Client Driver | `i2c_driver` structure | Line 1207-1216 |
| Regmap API | Register caching, bulk reads | Line 276-282 |
| IIO Subsystem | 7 channels (accel, gyro, temp) | Line 337-358 |
| Triggered Buffer | Hardware-triggered data capture | Line 779-822 |
| FIFO Support | Internal sensor FIFO | Line 459-522 |
| DMA Buffers | Coherent DMA allocation | Line 530-558 |
| Kconfig Options | Compile-time feature selection | Throughout |
| Power Management | Suspend/resume support | Line 1154-1183 |

---

## Driver Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                      USER SPACE                                      │
│                                                                      │
│   cat /sys/bus/iio/devices/iio:device0/in_accel_x_raw               │
│   cat /dev/iio:device0  (buffered data)                             │
└──────────────────────────────┬──────────────────────────────────────┘
                               │
                               ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    IIO SUBSYSTEM                                     │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────────────┐ │
│  │   Sysfs     │    │   Buffer    │    │      Trigger            │ │
│  │  Interface  │    │   (kfifo)   │◄───│  (data-ready IRQ)       │ │
│  └──────┬──────┘    └──────┬──────┘    └───────────┬─────────────┘ │
│         │                  │                       │                │
└─────────┼──────────────────┼───────────────────────┼────────────────┘
          │                  │                       │
          ▼                  ▼                       ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    MPU6050 DRIVER                                    │
│                                                                      │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────────────┐│
│  │  mpu6050_       │  │  mpu6050_       │  │  mpu6050_data_ready_ ││
│  │  read_raw()     │  │  trigger_       │  │  irq()               ││
│  │                 │  │  handler()      │  │                      ││
│  └────────┬────────┘  └────────┬────────┘  └──────────┬───────────┘│
│           │                    │                      │             │
│           └────────────────────┴──────────────────────┘             │
│                                │                                     │
│                                ▼                                     │
│                    ┌─────────────────────┐                          │
│                    │  Regmap Layer       │                          │
│                    │  (cached/volatile)  │                          │
│                    └──────────┬──────────┘                          │
└───────────────────────────────┼─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       I2C SUBSYSTEM                                  │
│                    i2c_smbus_read/write                             │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    MPU6050 HARDWARE                                  │
│           ┌─────────────────────────────────────────┐               │
│           │  Registers 0x00-0x75                    │               │
│           │  - Accel: 0x3B-0x40 (XYZ, 16-bit BE)   │               │
│           │  - Temp:  0x41-0x42 (16-bit BE)        │               │
│           │  - Gyro:  0x43-0x48 (XYZ, 16-bit BE)   │               │
│           │  - FIFO:  0x74 (up to 1024 bytes)      │               │
│           └─────────────────────────────────────────┘               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Code Walkthrough

### Part 1: Register Map (Lines 72-166)

```c
/* Configuration registers */
#define MPU6050_REG_SMPLRT_DIV      0x19    /* Sample Rate Divider */
#define MPU6050_REG_CONFIG          0x1A    /* Configuration */
#define MPU6050_REG_GYRO_CONFIG     0x1B    /* Gyroscope Configuration */
#define MPU6050_REG_ACCEL_CONFIG    0x1C    /* Accelerometer Configuration */
```

**Why these registers matter:**
- `SMPLRT_DIV`: Controls output data rate: `Rate = 1kHz / (1 + SMPLRT_DIV)`
- `GYRO_CONFIG`: Sets full-scale range (±250 to ±2000 °/s)
- `ACCEL_CONFIG`: Sets full-scale range (±2g to ±16g)

### Part 2: Scale Tables (Lines 190-203)

```c
static const int mpu6050_accel_scale_table[] = {
    598,    /* 2g:  9.80665 / 16384 * 1000000 */
    1197,   /* 4g:  9.80665 / 8192 * 1000000 */
    2394,   /* 8g */
    4788,   /* 16g */
};
```

**Why scale tables?**
IIO reports values in standard SI units. For acceleration:
- Raw value is LSBs from ADC
- Scale converts to m/s²
- Formula: `acceleration_m_s2 = raw * scale / 1000000`

**Example at ±2g range:**
- ADC is 16-bit signed: ±32768 counts for ±2g
- 1g = 9.80665 m/s²
- Scale = 9.80665 / 16384 * 1000000 = 598 μm/s² per LSB

### Part 3: Regmap Configuration (Lines 262-282)

```c
static bool mpu6050_volatile_reg(struct device *dev, unsigned int reg)
{
    switch (reg) {
    case MPU6050_REG_INT_STATUS:
    case MPU6050_REG_ACCEL_XOUT_H ... MPU6050_REG_GYRO_ZOUT_L:
        return true;    /* Don't cache - changes constantly */
    default:
        return false;   /* Cache these - configuration registers */
    }
}

static const struct regmap_config mpu6050_regmap_config = {
    .reg_bits = 8,
    .val_bits = 8,
    .volatile_reg = mpu6050_volatile_reg,
    .cache_type = REGCACHE_RBTREE,
};
```

**Why Regmap?**
1. **Abstraction**: Same API for I2C, SPI, MMIO
2. **Caching**: Avoid redundant reads for configuration registers
3. **Bulk operations**: Efficient multi-byte transfers
4. **Debugging**: Easy to trace register access

### Part 4: IIO Channel Definitions (Lines 301-358)

```c
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
        .sign = 's',    /* Signed */                                \
        .realbits = 16, /* 16-bit ADC */                            \
        .storagebits = 16,                                          \
        .endianness = IIO_BE,  /* MPU6050 is big-endian */         \
    },                                                              \
}
```

**Channel attributes explained:**

| Attribute | sysfs File | Purpose |
|-----------|------------|---------|
| `RAW` | `in_accel_x_raw` | Raw ADC value |
| `CALIBBIAS` | `in_accel_x_calibbias` | Per-axis offset |
| `SCALE` | `in_accel_scale` | Shared multiplier |
| `SAMP_FREQ` | `sampling_frequency` | Sample rate |

### Part 5: Reading Sensor Data (Lines 366-451)

```c
static int mpu6050_read_all_data(struct mpu6050_data *data)
{
    u8 buf[14];
    int ret;

    /* Burst read: 6 accel + 2 temp + 6 gyro = 14 bytes */
    ret = regmap_bulk_read(data->regmap, MPU6050_REG_ACCEL_XOUT_H, buf, 14);
    if (ret)
        return ret;

    /* Parse big-endian data */
    data->buffer.accel[0] = (s16)((buf[0] << 8) | buf[1]);
    data->buffer.accel[1] = (s16)((buf[2] << 8) | buf[3]);
    /* ... */
}
```

**Why burst read?**
- Single I2C transaction for all data
- Consistent timestamp across all axes
- Faster than individual register reads

### Part 6: Triggered Buffer Handler (Lines 779-822)

```c
static irqreturn_t mpu6050_trigger_handler(int irq, void *p)
{
    struct iio_poll_func *pf = p;
    struct iio_dev *indio_dev = pf->indio_dev;
    struct mpu6050_data *data = iio_priv(indio_dev);

    /* Option 1: Read from FIFO (if enabled) */
#ifdef CONFIG_MPU6050_FIFO
    if (data->fifo_enabled) {
        mpu6050_fifo_read_data(data, fifo_buf, MPU6050_SAMPLE_SIZE);
    }
#endif

    /* Option 2: Direct register read */
    mpu6050_read_all_data(data);

    /* Push to IIO buffer with timestamp */
    iio_push_to_buffers_with_timestamp(indio_dev, &data->buffer,
                                       iio_get_time_ns(indio_dev));

    iio_trigger_notify_done(indio_dev->trig);
    return IRQ_HANDLED;
}
```

**Triggered buffer flow:**
1. Hardware IRQ fires (data ready)
2. `mpu6050_data_ready_irq()` calls `iio_trigger_poll()`
3. IIO core schedules `mpu6050_trigger_handler()`
4. Handler reads data and pushes to buffer
5. Userspace reads from `/dev/iio:device0`

### Part 7: Kconfig Feature Selection

The driver uses conditional compilation for optional features:

```c
#ifdef CONFIG_MPU6050_FIFO
static int mpu6050_fifo_enable(struct mpu6050_data *data, bool enable)
{
    /* FIFO implementation */
}
#endif

#ifdef CONFIG_MPU6050_DMA
static int mpu6050_dma_alloc(struct mpu6050_data *data)
{
    data->dma_buffer = dma_alloc_coherent(dev, size, &handle, GFP_KERNEL);
}
#endif
```

**Available Kconfig options:**
- `CONFIG_MPU6050`: Enable driver (tristate)
- `CONFIG_MPU6050_IRQ`: Enable interrupt support
- `CONFIG_MPU6050_TRIGGER`: Enable hardware trigger
- `CONFIG_MPU6050_DMA`: Enable DMA buffer support
- `CONFIG_MPU6050_FIFO`: Use internal FIFO
- `CONFIG_MPU6050_DEBUG`: Enable debug output

---

## Device Tree Configuration

```dts
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    mpu6050: imu@68 {
        compatible = "invensense,mpu6050";
        reg = <0x68>;

        /* Interrupt configuration */
        interrupt-parent = <&gpio>;
        interrupts = <4 IRQ_TYPE_EDGE_RISING>;

        /* Optional: VDDIO supply */
        vddio-supply = <&reg_3v3>;
    };
};
```

---

## Userspace Usage

### Read Single Values
```bash
# Raw accelerometer X
cat /sys/bus/iio/devices/iio:device0/in_accel_x_raw

# Scale factor
cat /sys/bus/iio/devices/iio:device0/in_accel_scale

# Calculate: accel_m_s2 = raw * scale
# Example: 16384 * 0.000598 = 9.8 m/s² (1g)
```

### Buffered Data
```bash
# Enable channels
echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_x_en
echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_y_en
echo 1 > /sys/bus/iio/devices/iio:device0/scan_elements/in_accel_z_en

# Set trigger (if using hardware trigger)
echo "mpu6050-dev0" > /sys/bus/iio/devices/iio:device0/trigger/current_trigger

# Enable buffer
echo 100 > /sys/bus/iio/devices/iio:device0/buffer/length
echo 1 > /sys/bus/iio/devices/iio:device0/buffer/enable

# Read binary data
cat /dev/iio:device0 | hexdump -C
```

---

## Key Learning Points

### 1. Regmap Benefits
- Abstract bus (I2C/SPI) from driver logic
- Automatic register caching for non-volatile registers
- Easy debugging and tracing

### 2. IIO Channel Design
- `info_mask_separate`: Per-channel attributes
- `info_mask_shared_by_type`: Shared by all channels of same type
- `scan_type`: Defines data format for buffers

### 3. Triggered Buffers
- Hardware trigger → consistent timestamps
- Efficient bulk data transfer
- Standard interface for data acquisition

### 4. Kconfig Patterns
- Optional features at compile time
- Reduces code size for constrained systems
- Clear feature documentation

---

## Exercises

1. **Add self-test**: Implement the MPU6050's built-in self-test feature
2. **Add motion detection**: Configure wake-on-motion interrupt
3. **Add I2C bypass**: Allow direct access to AUX I2C for magnetometer
4. **Add SPI support**: The MPU6050 also supports SPI interface
