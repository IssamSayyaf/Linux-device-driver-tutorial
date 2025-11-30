# HC-SR04 Ultrasonic Distance Sensor Tutorial

Complete guide to understanding the GPIO-based ultrasonic sensor driver with precise timing.

## What This Driver Demonstrates

| Feature | Implementation | Purpose |
|---------|---------------|---------|
| GPIO Subsystem | `gpiod_*` API | Modern GPIO control |
| Platform Driver | `platform_driver` | Non-bus device binding |
| IRQ Handling | Both edge interrupts | Echo pulse measurement |
| High-res Timing | `ktime_get()` | Microsecond precision |
| IIO Distance Channel | `IIO_DISTANCE` | Standard interface |
| Completion API | `wait_for_completion_timeout` | Sync with IRQ |

---

## How HC-SR04 Works

```
┌─────────────────────────────────────────────────────────────────────┐
│                    HC-SR04 MEASUREMENT CYCLE                         │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  TRIGGER ────┐  ┌──────────────────────────────────────────────────  │
│              └──┘                                                    │
│              │←─→│                                                   │
│              10µs trigger pulse                                      │
│                                                                      │
│                        ← delay (~460µs) →                           │
│                                                                      │
│  ECHO    ────────────────┐                    ┌────────────────────  │
│                          └────────────────────┘                      │
│                          │←─── echo width ───→│                      │
│                          │   proportional to   │                      │
│                          │     distance        │                      │
│                                                                      │
│  Ultrasound:                                                         │
│                                                                      │
│      Sensor )))────────────────────────────→ Object                 │
│              ←────────────────────────────((( (reflection)          │
│                                                                      │
│  Distance = (echo_time × speed_of_sound) / 2                        │
│           = (echo_time_µs × 343 m/s) / (2 × 1,000,000)             │
│           ≈ echo_time_µs / 58  (in cm)                              │
│           ≈ echo_time_µs × 10 / 58  (in mm)                         │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Driver Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER SPACE                                   │
│   cat /sys/bus/iio/devices/iio:device0/in_distance_raw              │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                         IIO CORE                                     │
│              hc_sr04_read_raw() called                              │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       HC-SR04 DRIVER                                 │
│                                                                      │
│  1. ┌────────────────────────────────────────────┐                  │
│     │  reinit_completion(&echo_complete)          │ ◄── Reset state │
│     └────────────────────────────────────────────┘                  │
│                          │                                           │
│  2.                      ▼                                           │
│     ┌────────────────────────────────────────────┐                  │
│     │  gpiod_set_value(trigger, 1)               │                  │
│     │  udelay(10)                                │ ◄── 10µs pulse   │
│     │  gpiod_set_value(trigger, 0)               │                  │
│     └────────────────────────────────────────────┘                  │
│                          │                                           │
│  3.                      ▼                                           │
│     ┌────────────────────────────────────────────┐                  │
│     │  wait_for_completion_timeout(30ms)         │ ◄── Wait for IRQ │
│     └────────────────────────────────────────────┘                  │
│                          │                                           │
│             ┌────────────┴────────────┐                             │
│             │    IRQ Handler          │                             │
│             │                         │                             │
│             │  RISING EDGE:           │                             │
│             │    echo_start = ktime() │                             │
│             │                         │                             │
│             │  FALLING EDGE:          │                             │
│             │    echo_end = ktime()   │                             │
│             │    complete()           │                             │
│             └─────────────────────────┘                             │
│                          │                                           │
│  4.                      ▼                                           │
│     ┌────────────────────────────────────────────┐                  │
│     │  distance_mm = echo_duration_us × 10 / 58  │ ◄── Calculate   │
│     └────────────────────────────────────────────┘                  │
│                          │                                           │
│                          ▼                                           │
│                    Return to IIO                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Code Walkthrough

### Part 1: Device Structure (Lines 49-71)

```c
struct hc_sr04_data {
    struct device *dev;
    struct gpio_desc *trigger_gpio;   /* Output: trigger pulse */
    struct gpio_desc *echo_gpio;      /* Input: echo pulse */
    int echo_irq;                     /* IRQ number for echo GPIO */

    /* Measurement state */
    struct completion echo_complete;  /* Signals echo received */
    ktime_t echo_start;               /* Rising edge timestamp */
    ktime_t echo_end;                 /* Falling edge timestamp */
    bool echo_received;               /* Valid measurement flag */

    /* Configuration */
    int temperature;                  /* For speed-of-sound correction */

    /* Statistics */
    unsigned long measurements;
    unsigned long timeouts;
    unsigned long errors;

    struct mutex lock;                /* Serialize measurements */
};
```

**Why these fields?**
- `ktime_t` provides nanosecond precision needed for µs measurements
- `completion` synchronizes IRQ handler with measurement function
- `mutex` prevents concurrent measurements (sensor needs recovery time)

### Part 2: IRQ Handler (Lines 79-97)

```c
static irqreturn_t hc_sr04_echo_irq(int irq, void *dev_id)
{
    struct hc_sr04_data *data = dev_id;
    int echo_level;

    echo_level = gpiod_get_value(data->echo_gpio);

    if (echo_level) {
        /* Rising edge - echo pulse started */
        data->echo_start = ktime_get();
    } else {
        /* Falling edge - echo pulse ended */
        data->echo_end = ktime_get();
        data->echo_received = true;
        complete(&data->echo_complete);  /* Wake measurement function */
    }

    return IRQ_HANDLED;
}
```

**Critical timing considerations:**
- IRQ handler must be fast (no sleeping!)
- `ktime_get()` is interrupt-safe
- `complete()` wakes the waiting thread

### Part 3: Distance Measurement (Lines 105-177)

```c
static int hc_sr04_measure_distance(struct hc_sr04_data *data, int *distance_mm)
{
    ktime_t echo_duration;
    s64 echo_us;
    int ret;

    mutex_lock(&data->lock);

    /* Step 1: Reset state */
    reinit_completion(&data->echo_complete);
    data->echo_received = false;

    /* Step 2: Generate 10µs trigger pulse */
    gpiod_set_value(data->trigger_gpio, 1);
    udelay(TRIGGER_PULSE_US);  /* 10µs - busy wait is OK here */
    gpiod_set_value(data->trigger_gpio, 0);

    /* Step 3: Wait for echo (IRQ will complete()) */
    ret = wait_for_completion_timeout(&data->echo_complete,
                                      msecs_to_jiffies(30));
    if (!ret) {
        /* Timeout - no object detected or too far */
        data->timeouts++;
        mutex_unlock(&data->lock);
        return -ETIMEDOUT;
    }

    /* Step 4: Calculate distance */
    echo_duration = ktime_sub(data->echo_end, data->echo_start);
    echo_us = ktime_to_us(echo_duration);

    /*
     * Physics: distance = (time × speed) / 2
     * Speed of sound at 20°C = 343.2 m/s = 0.03432 cm/µs
     * distance_cm = echo_us / 58.14
     * distance_mm = echo_us × 10 / 58
     */
    *distance_mm = (int)((echo_us * 10) / 58);

    mutex_unlock(&data->lock);
    return 0;
}
```

**Why `udelay()` not `usleep_range()`?**
- `udelay()` for short, precise delays < 20µs
- `usleep_range()` allows sleeping (context switch) - not precise enough

### Part 4: Temperature Compensation (Lines 161-171)

```c
if (data->temperature != 20000) {
    /* Speed of sound = 331.3 + 0.606 × T (m/s) */
    int temp_c = data->temperature / 1000;
    int speed = 3313 + 6 * temp_c;  /* 0.1 m/s units */
    *distance_mm = (echo_us * speed) / 20000;
}
```

**Why temperature matters:**
- Sound speed varies with temperature
- At 0°C: 331 m/s
- At 20°C: 343 m/s
- At 40°C: 355 m/s
- Error without compensation: ~0.6% per 10°C

### Part 5: IIO Channel Definition (Lines 185-197)

```c
static const struct iio_chan_spec hc_sr04_channels[] = {
    {
        .type = IIO_DISTANCE,
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
    },
    {
        .type = IIO_TEMP,
        .output = 1,    /* This is a configuration INPUT from userspace */
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW),
    },
};
```

**Channel design:**
- `IIO_DISTANCE`: Reports measured distance in mm
- `IIO_TEMP` with `output = 1`: Allows setting temperature for compensation

### Part 6: Probe Function (Lines 320-391)

```c
static int hc_sr04_probe(struct platform_device *pdev)
{
    /* Get GPIOs from device tree */
    data->trigger_gpio = devm_gpiod_get(dev, "trigger", GPIOD_OUT_LOW);
    data->echo_gpio = devm_gpiod_get(dev, "echo", GPIOD_IN);

    /* Get IRQ number from GPIO */
    data->echo_irq = gpiod_to_irq(data->echo_gpio);

    /* Request both-edge interrupt */
    ret = devm_request_irq(dev, data->echo_irq, hc_sr04_echo_irq,
                           IRQF_TRIGGER_RISING | IRQF_TRIGGER_FALLING,
                           DRIVER_NAME, data);
}
```

**GPIO descriptor API benefits:**
- `devm_gpiod_get()` - Auto cleanup on driver removal
- Handles device tree binding
- Polarity inversion handled automatically

---

## Device Tree Configuration

```dts
/ {
    hc_sr04: ultrasonic-sensor {
        compatible = "hc-sr04";

        /* GPIO connections */
        trigger-gpios = <&gpio 23 GPIO_ACTIVE_HIGH>;
        echo-gpios = <&gpio 24 GPIO_ACTIVE_HIGH>;

        status = "okay";
    };
};
```

---

## Userspace Usage

### Read Distance
```bash
# Read distance in mm
cat /sys/bus/iio/devices/iio:device0/in_distance_raw
# Output: 1234 (meaning 1234 mm = 123.4 cm)

# Get scale (conversion to meters)
cat /sys/bus/iio/devices/iio:device0/in_distance_scale
# Output: 0.001000 (multiply by this to get meters)
```

### Set Temperature for Correction
```bash
# Set ambient temperature to 25°C (25000 milli-degrees)
echo 25000 > /sys/bus/iio/devices/iio:device0/out_temp_raw

# Now measurements will use 25°C for speed-of-sound calculation
```

### View Statistics
```bash
cat /sys/bus/iio/devices/iio:device0/statistics
# Output:
# measurements: 1523
# timeouts: 12
# errors: 0
```

---

## Timing Analysis

```
┌───────────────────────────────────────────────────────────────────┐
│                      TIMING DIAGRAM                                │
├───────────────────────────────────────────────────────────────────┤
│                                                                    │
│  Time →   0µs     10µs            ~460µs         ~580µs (2cm)     │
│           │        │                │              │               │
│  TRIGGER  ┌────────┐                                               │
│           │        │                                               │
│  ─────────┘        └────────────────────────────────────────────   │
│                                                                    │
│  ECHO                               ┌──────────────┐               │
│                                     │              │               │
│  ───────────────────────────────────┘              └────────────   │
│                                     │←─ 116µs ≈2cm─→│              │
│                                                                    │
│  For 4 meters (max range):                                         │
│  echo_time = 4m × 2 / 343 m/s = 23.3 ms                           │
│                                                                    │
│  Minimum measurement interval: ~60ms (to let echoes fade)         │
│                                                                    │
└───────────────────────────────────────────────────────────────────┘
```

---

## Key Learning Points

### 1. GPIO Descriptor API
```c
/* Old (deprecated) */
gpio_request(17, "trigger");
gpio_direction_output(17, 0);

/* New (use this) */
gpio = devm_gpiod_get(dev, "trigger", GPIOD_OUT_LOW);
gpiod_set_value(gpio, 1);
```

### 2. Completion vs Mutex
- **Completion**: For signaling between contexts (IRQ → thread)
- **Mutex**: For protecting shared data (serializing access)

### 3. IRQ Trigger Modes
```c
IRQF_TRIGGER_RISING   /* Interrupt on LOW→HIGH */
IRQF_TRIGGER_FALLING  /* Interrupt on HIGH→LOW */
/* Both are needed for echo pulse measurement */
```

### 4. High-Resolution Timing
```c
ktime_t start = ktime_get();      /* Get timestamp */
/* ... operation ... */
ktime_t end = ktime_get();
s64 us = ktime_to_us(ktime_sub(end, start));  /* Duration in µs */
```

---

## Hardware Considerations

| Parameter | Value | Notes |
|-----------|-------|-------|
| Range | 2cm - 400cm | <2cm unreliable |
| Accuracy | ±3mm | At close range |
| Trigger pulse | 10µs minimum | Must be precise |
| Echo timeout | 30-38ms | ~5-6m equivalent |
| Refresh rate | ~15 Hz max | Need echo to fade |
| Operating voltage | 5V | Some 3.3V variants exist |

**Level shifting note:** Echo pin outputs 5V. For 3.3V GPIOs, use a voltage divider or level shifter.

---

## Exercises

1. **Add averaging**: Implement multi-sample averaging for stability
2. **Add filtering**: Reject outliers using median filter
3. **Add triggered buffer**: Support continuous sampling via IIO buffer
4. **Add multiple sensors**: Support multiple HC-SR04 on different GPIOs
5. **Add events**: Generate IIO event when object detected within threshold
