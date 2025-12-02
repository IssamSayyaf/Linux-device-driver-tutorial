# HC-SR04 Ultrasonic Distance Sensor Driver - UML Documentation

## Overview

The HC-SR04 driver is a GPIO-based Platform Device Driver that integrates with the Linux IIO (Industrial I/O) subsystem to provide distance measurement functionality.

**Driver Type:** GPIO-based IIO Platform Driver
**File:** `drivers/sensors/ultrasonic/hc_sr04.c`
**Subsystems:** GPIO, IIO, IRQ, ktime

---

## 1. Class Diagram (Data Structures)

```mermaid
classDiagram
    class hc_sr04_data {
        +struct device *dev
        +struct gpio_desc *trigger_gpio
        +struct gpio_desc *echo_gpio
        +int echo_irq
        +struct completion echo_complete
        +ktime_t echo_start
        +ktime_t echo_end
        +bool echo_received
        +int temperature
        +unsigned long measurements
        +unsigned long timeouts
        +unsigned long errors
        +struct mutex lock
    }

    class iio_dev {
        +const char *name
        +const struct iio_info *info
        +int modes
        +struct iio_chan_spec *channels
        +int num_channels
        +void *priv
    }

    class iio_info {
        +read_raw()
        +write_raw()
        +attrs
    }

    class iio_chan_spec {
        +enum iio_chan_type type
        +long info_mask_separate
        +long info_mask_shared_by_type
    }

    class platform_driver {
        +probe()
        +driver: device_driver
    }

    class gpio_desc {
        <<kernel structure>>
        +GPIO descriptor
    }

    hc_sr04_data --> iio_dev : embedded in iio_priv
    hc_sr04_data --> gpio_desc : trigger_gpio
    hc_sr04_data --> gpio_desc : echo_gpio
    iio_dev --> iio_info : info
    iio_dev --> iio_chan_spec : channels
    platform_driver --> hc_sr04_data : creates
```

---

## 2. Component Diagram (Subsystem Integration)

```mermaid
flowchart TB
    subgraph UserSpace["User Space"]
        APP[Application]
        SYSFS["/sys/bus/iio/devices/iio:deviceX"]
    end

    subgraph KernelSpace["Kernel Space"]
        subgraph IIO["IIO Subsystem"]
            IIO_CORE[IIO Core]
            IIO_SYSFS[IIO Sysfs Interface]
        end

        subgraph Driver["HC-SR04 Driver"]
            PROBE[hc_sr04_probe]
            READ[hc_sr04_read_raw]
            MEASURE[hc_sr04_measure_distance]
            IRQ_HANDLER[hc_sr04_echo_irq]
        end

        subgraph GPIO["GPIO Subsystem"]
            GPIOD[GPIO Descriptor API]
            GPIO_IRQ[GPIO to IRQ]
        end

        subgraph Platform["Platform Bus"]
            PLATFORM_DRV[platform_driver]
            DT[Device Tree Binding]
        end
    end

    subgraph Hardware["Hardware"]
        HC_SR04[HC-SR04 Sensor]
        TRIGGER[Trigger Pin]
        ECHO[Echo Pin]
    end

    APP --> SYSFS
    SYSFS --> IIO_SYSFS
    IIO_SYSFS --> IIO_CORE
    IIO_CORE --> READ
    READ --> MEASURE
    MEASURE --> GPIOD
    IRQ_HANDLER --> GPIO_IRQ
    PLATFORM_DRV --> PROBE
    DT --> PLATFORM_DRV
    GPIOD --> TRIGGER
    GPIO_IRQ --> ECHO
    HC_SR04 --- TRIGGER
    HC_SR04 --- ECHO
```

---

## 3. Sequence Diagram (Distance Measurement)

```mermaid
sequenceDiagram
    participant App as User Application
    participant Sysfs as IIO Sysfs
    participant IIO as IIO Core
    participant Driver as HC-SR04 Driver
    participant GPIO as GPIO Subsystem
    participant IRQ as IRQ Subsystem
    participant HW as HC-SR04 Hardware

    App->>Sysfs: Read in_distance_raw
    Sysfs->>IIO: iio_read_channel_raw()
    IIO->>Driver: hc_sr04_read_raw(IIO_CHAN_INFO_RAW)
    Driver->>Driver: hc_sr04_measure_distance()

    Note over Driver: Acquire mutex lock
    Driver->>Driver: reinit_completion()
    Driver->>Driver: Reset echo_received = false

    Driver->>GPIO: gpiod_set_value(trigger, 1)
    Note over HW: Trigger pulse starts
    Driver->>Driver: udelay(10)
    Driver->>GPIO: gpiod_set_value(trigger, 0)
    Note over HW: Trigger pulse ends (10µs)

    HW-->>HW: Ultrasonic burst sent
    HW-->>HW: Echo returns

    HW->>IRQ: Echo rising edge
    IRQ->>Driver: hc_sr04_echo_irq()
    Driver->>Driver: echo_start = ktime_get()

    HW->>IRQ: Echo falling edge
    IRQ->>Driver: hc_sr04_echo_irq()
    Driver->>Driver: echo_end = ktime_get()
    Driver->>Driver: echo_received = true
    Driver->>Driver: complete(&echo_complete)

    Driver->>Driver: wait_for_completion_timeout()
    Note over Driver: Calculate distance
    Driver->>Driver: distance_mm = (echo_us * 10) / 58
    Driver->>Driver: Release mutex lock

    Driver-->>IIO: Return distance (mm)
    IIO-->>Sysfs: Return value
    Sysfs-->>App: Distance in mm
```

---

## 4. State Machine Diagram (Measurement States)

```mermaid
stateDiagram-v2
    [*] --> Idle: Driver Loaded

    Idle --> WaitingForMeasurement: User reads distance

    WaitingForMeasurement --> TriggerPulse: Acquire mutex

    TriggerPulse --> WaitingForEcho: 10µs pulse sent
    note right of TriggerPulse
        - Set trigger GPIO high
        - Wait 10µs
        - Set trigger GPIO low
    end note

    WaitingForEcho --> EchoReceived: Echo interrupt (falling edge)
    WaitingForEcho --> Timeout: 30ms elapsed

    note right of WaitingForEcho
        Rising edge: Record start time
        Falling edge: Record end time
    end note

    EchoReceived --> CalculateDistance: Echo complete

    CalculateDistance --> Idle: Return result, release mutex
    note right of CalculateDistance
        Distance = (echo_time_us × speed_of_sound) / 2
        At 20°C: distance_mm = echo_us × 10 / 58
    end note

    Timeout --> Idle: Return -ETIMEDOUT, release mutex

    state EchoReceived {
        [*] --> RecordEndTime
        RecordEndTime --> SignalComplete
    }
```

---

## 5. Activity Diagram (Probe Function)

```mermaid
flowchart TD
    START([Start Probe]) --> ALLOC[Allocate IIO device with devm_iio_device_alloc]
    ALLOC --> GET_PRIV[Get private data pointer with iio_priv]
    GET_PRIV --> INIT_MUTEX[Initialize mutex]
    INIT_MUTEX --> INIT_COMPLETE[Initialize completion]
    INIT_COMPLETE --> SET_TEMP[Set default temperature 20°C]

    SET_TEMP --> GET_TRIGGER{Get trigger GPIO}
    GET_TRIGGER -->|Success| GET_ECHO{Get echo GPIO}
    GET_TRIGGER -->|Failure| ERROR1[Return error]

    GET_ECHO -->|Success| GET_IRQ{Get IRQ from echo GPIO}
    GET_ECHO -->|Failure| ERROR2[Return error]

    GET_IRQ -->|Success| REQ_IRQ{Request IRQ with both edges}
    GET_IRQ -->|Failure| ERROR3[Return error]

    REQ_IRQ -->|Success| SETUP_IIO[Setup IIO device]
    REQ_IRQ -->|Failure| ERROR4[Return error]

    SETUP_IIO --> SET_NAME[Set name = hc-sr04]
    SET_NAME --> SET_INFO[Set iio_info structure]
    SET_INFO --> SET_MODE[Set mode = INDIO_DIRECT_MODE]
    SET_MODE --> SET_CHANNELS[Set channels array]

    SET_CHANNELS --> REGISTER{Register IIO device}
    REGISTER -->|Success| LOG[Log success message]
    REGISTER -->|Failure| ERROR5[Return error]

    LOG --> DONE([Return 0])

    ERROR1 --> FAIL([Return error code])
    ERROR2 --> FAIL
    ERROR3 --> FAIL
    ERROR4 --> FAIL
    ERROR5 --> FAIL
```

---

## 6. Timing Diagram (Measurement Timing)

```
                    10µs
                   |←→|
    Trigger Pin _____|‾‾‾|_______________________________________________
                     ↑
                     Trigger pulse start

                              Variable (proportional to distance)
                         |←─────────────────────→|
    Echo Pin    _________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|_______________________
                         ↑                       ↑
                    Rising edge              Falling edge
                   (echo_start)              (echo_end)

    Time        ─────────┬───────────────────────┬─────────────→
                         t1                      t2

    Echo Duration = t2 - t1 (in microseconds)
    Distance (cm) = Echo Duration / 58
    Distance (mm) = Echo Duration × 10 / 58

    Maximum range: ~4 meters (echo timeout: 30ms)
    Minimum range: ~2 cm
```

---

## 7. IRQ Handler State Diagram

```mermaid
stateDiagram-v2
    [*] --> CheckLevel: IRQ Triggered

    CheckLevel --> RisingEdge: echo_level == 1
    CheckLevel --> FallingEdge: echo_level == 0

    RisingEdge --> RecordStart: Record echo_start
    RecordStart --> IRQHandled1: Return IRQ_HANDLED

    FallingEdge --> RecordEnd: Record echo_end
    RecordEnd --> SetReceived: Set echo_received = true
    SetReceived --> Complete: Call complete()
    Complete --> IRQHandled2: Return IRQ_HANDLED

    IRQHandled1 --> [*]
    IRQHandled2 --> [*]
```

---

## 8. Data Flow Diagram

```mermaid
flowchart LR
    subgraph Input
        USER[User Application]
        TEMP[Temperature Config]
    end

    subgraph Processing["Driver Processing"]
        TRIG[Generate Trigger Pulse]
        WAIT[Wait for Echo]
        CALC[Calculate Distance]
        COMP[Temperature Compensation]
    end

    subgraph Output
        DIST[Distance in mm]
        STATS[Statistics]
    end

    subgraph Hardware
        SENSOR[HC-SR04 Sensor]
    end

    USER -->|Read distance| TRIG
    TEMP -->|Configure| COMP
    TRIG -->|10µs pulse| SENSOR
    SENSOR -->|Echo pulse| WAIT
    WAIT -->|Echo time| CALC
    CALC -->|Raw distance| COMP
    COMP --> DIST
    CALC --> STATS
    DIST --> USER
```

---

## 9. IIO Channel Structure

```mermaid
classDiagram
    class IIO_DISTANCE_Channel {
        +type: IIO_DISTANCE
        +info_mask_separate: RAW | SCALE
        +info_mask_shared_by_type: SAMP_FREQ
        -Returns distance in millimeters
    }

    class IIO_TEMP_Channel {
        +type: IIO_TEMP
        +output: 1 (writable)
        +info_mask_separate: RAW
        -For temperature compensation
        -Range: -40°C to 85°C
    }

    class hc_sr04_channels {
        +Distance Channel
        +Temperature Config Channel
    }

    hc_sr04_channels --> IIO_DISTANCE_Channel
    hc_sr04_channels --> IIO_TEMP_Channel
```

---

## 10. Error Handling Flow

```mermaid
flowchart TD
    START[Start Measurement] --> LOCK[Acquire Mutex]
    LOCK --> TRIGGER[Send Trigger Pulse]
    TRIGGER --> WAIT[Wait for Completion]

    WAIT --> TIMEOUT{Timeout?}
    TIMEOUT -->|Yes| INC_TO[Increment timeouts counter]
    INC_TO --> RET_TO[Return -ETIMEDOUT]

    TIMEOUT -->|No| CHECK_RECV{Echo Received?}
    CHECK_RECV -->|No| INC_ERR[Increment errors counter]
    INC_ERR --> RET_EIO[Return -EIO]

    CHECK_RECV -->|Yes| VALIDATE{Valid timing?}
    VALIDATE -->|No 0-30000µs| INC_RANGE[Increment errors counter]
    INC_RANGE --> RET_RANGE[Return -ERANGE]

    VALIDATE -->|Yes| CALC[Calculate Distance]
    CALC --> INC_MEAS[Increment measurements counter]
    INC_MEAS --> UNLOCK[Release Mutex]
    UNLOCK --> RETURN[Return distance_mm]

    RET_TO --> UNLOCK2[Release Mutex]
    RET_EIO --> UNLOCK2
    RET_RANGE --> UNLOCK2
    UNLOCK2 --> FAIL[Return Error]
```

---

## 11. Device Tree Binding

```
Device Tree Structure:

ultrasonic-sensor {
    compatible = "hc-sr04", "elecfreaks,hc-sr04";
    trigger-gpios = <&gpio 17 GPIO_ACTIVE_HIGH>;
    echo-gpios = <&gpio 27 GPIO_ACTIVE_HIGH>;
};

┌─────────────────────────────────────────────┐
│              Device Tree Node               │
├─────────────────────────────────────────────┤
│  compatible: "hc-sr04"                      │
│                                             │
│  ┌─────────────────┐   ┌─────────────────┐  │
│  │  trigger-gpios  │   │   echo-gpios    │  │
│  │   GPIO Output   │   │   GPIO Input    │  │
│  │   Active High   │   │   Active High   │  │
│  └────────┬────────┘   └────────┬────────┘  │
│           │                     │           │
└───────────┼─────────────────────┼───────────┘
            │                     │
            ▼                     ▼
    ┌───────────────────────────────────┐
    │          HC-SR04 Sensor           │
    │   ┌───────┐         ┌───────┐     │
    │   │ TRIG  │         │ ECHO  │     │
    │   └───────┘         └───────┘     │
    │         ┌───────────────┐         │
    │         │  Ultrasonic   │         │
    │         │  Transducers  │         │
    │         └───────────────┘         │
    └───────────────────────────────────┘
```

---

## 12. Physics of Operation

```
Speed of Sound Calculation:

v = 331.3 + 0.606 × T (m/s)

Where T = temperature in °C

At 20°C:
v = 331.3 + 0.606 × 20 = 343.42 m/s = 0.03434 cm/µs

Distance Calculation:

       Echo Time (µs) × Speed of Sound (cm/µs)
Distance = ─────────────────────────────────────
                          2

For 20°C (simplified):
Distance (cm) = Echo Time (µs) / 58
Distance (mm) = Echo Time (µs) × 10 / 58

Temperature Compensation in Driver:
┌──────────────────────────────────────────────┐
│  speed = 3313 + 6 × temp_c  (0.1 m/s units)  │
│  distance_mm = (echo_us × speed) / 20000     │
└──────────────────────────────────────────────┘
```

---

## Summary

The HC-SR04 driver demonstrates:

1. **GPIO Integration** - Using GPIO descriptor API for trigger/echo pins
2. **Interrupt Handling** - Both-edge triggered IRQ for echo measurement
3. **IIO Subsystem** - Standard sensor interface for userspace
4. **High-Resolution Timing** - Using ktime for microsecond accuracy
5. **Synchronization** - Mutex and completion primitives
6. **Error Handling** - Timeout and validation with statistics
7. **Device Tree** - Platform device binding and configuration
