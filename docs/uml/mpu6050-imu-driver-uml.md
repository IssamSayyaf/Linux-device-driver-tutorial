# MPU6050 IMU Driver - UML Documentation

## Overview

The MPU6050 driver is an I2C client driver that integrates with the Linux IIO (Industrial I/O) subsystem to provide 6-axis motion tracking (accelerometer + gyroscope) and temperature sensing functionality.

**Driver Type:** I2C Client IIO Driver
**File:** `drivers/sensors/mpu6050/mpu6050.c`
**Subsystems:** I2C, IIO, Regmap, Triggered Buffer, PM Runtime

---

## 1. Class Diagram (Data Structures)

```mermaid
classDiagram
    class mpu6050_data {
        +struct i2c_client *client
        +struct regmap *regmap
        +u8 accel_fs
        +u8 gyro_fs
        +u16 sample_rate
        +s16 accel_offset[3]
        +s16 gyro_offset[3]
        +u8 *dma_buffer
        +dma_addr_t dma_handle
        +size_t dma_buf_size
        +struct buffer buffer
        +int irq
        +bool irq_enabled
        +struct iio_trigger *trig
        +bool fifo_enabled
        +u16 fifo_count
    }

    class buffer_struct {
        +s16 accel[3]
        +s16 gyro[3]
        +s16 temp
        +s64 timestamp
    }

    class iio_dev {
        +const char *name
        +const struct iio_info *info
        +int modes
        +struct iio_chan_spec *channels
        +int num_channels
        +struct iio_trigger *trig
        +void *priv
    }

    class iio_chan_spec {
        +enum iio_chan_type type
        +int channel2
        +int scan_index
        +struct iio_scan_type scan_type
        +long info_mask_separate
        +long info_mask_shared_by_type
    }

    class iio_trigger {
        +const char *name
        +struct iio_trigger_ops *ops
        +set_trigger_state()
    }

    class regmap {
        +regmap_read()
        +regmap_write()
        +regmap_bulk_read()
        +regmap_update_bits()
    }

    class i2c_client {
        +struct device dev
        +unsigned short addr
        +int irq
    }

    mpu6050_data --> i2c_client : client
    mpu6050_data --> regmap : regmap
    mpu6050_data --> buffer_struct : buffer
    mpu6050_data --> iio_trigger : trig
    mpu6050_data --> iio_dev : embedded in iio_priv
    iio_dev --> iio_chan_spec : channels
    iio_dev --> iio_trigger : trig
```

---

## 2. Component Diagram (System Architecture)

```mermaid
flowchart TB
    subgraph UserSpace["User Space"]
        APP[Application]
        SYSFS["/sys/bus/iio/devices/iio:deviceX"]
        CHARDEV["/dev/iio:deviceX"]
    end

    subgraph KernelSpace["Kernel Space"]
        subgraph IIO["IIO Subsystem"]
            IIO_CORE[IIO Core]
            IIO_BUFFER[IIO Buffer]
            IIO_TRIGGER[IIO Trigger]
            IIO_SYSFS[IIO Sysfs]
        end

        subgraph Driver["MPU6050 Driver"]
            READ_RAW[read_raw callback]
            WRITE_RAW[write_raw callback]
            TRIGGER_HANDLER[Trigger Handler]
            IRQ_HANDLER[Data Ready IRQ]
            HW_INIT[Hardware Init]
        end

        subgraph I2C["I2C Subsystem"]
            I2C_CORE[I2C Core]
            I2C_ADAPTER[I2C Adapter]
        end

        subgraph Regmap["Regmap"]
            REGMAP_I2C[Regmap I2C]
            REG_CACHE[Register Cache]
        end
    end

    subgraph Hardware["Hardware"]
        MPU6050[MPU6050 Sensor]
        ACCEL[Accelerometer]
        GYRO[Gyroscope]
        TEMP[Temperature]
        FIFO[1024B FIFO]
    end

    APP --> SYSFS
    APP --> CHARDEV
    SYSFS --> IIO_SYSFS
    CHARDEV --> IIO_BUFFER
    IIO_SYSFS --> IIO_CORE
    IIO_BUFFER --> IIO_CORE
    IIO_TRIGGER --> TRIGGER_HANDLER
    IIO_CORE --> READ_RAW
    IIO_CORE --> WRITE_RAW
    READ_RAW --> REGMAP_I2C
    WRITE_RAW --> REGMAP_I2C
    TRIGGER_HANDLER --> REGMAP_I2C
    IRQ_HANDLER --> IIO_TRIGGER
    REGMAP_I2C --> REG_CACHE
    REGMAP_I2C --> I2C_CORE
    I2C_CORE --> I2C_ADAPTER
    I2C_ADAPTER --> MPU6050
    MPU6050 --> ACCEL
    MPU6050 --> GYRO
    MPU6050 --> TEMP
    MPU6050 --> FIFO
```

---

## 3. Register Map Diagram

```
MPU6050 Register Map:

Address  Name              Description
─────────────────────────────────────────────────────────
0x19     SMPLRT_DIV       Sample Rate Divider
0x1A     CONFIG           Configuration (DLPF, EXT_SYNC)
0x1B     GYRO_CONFIG      Gyroscope Configuration (FS_SEL)
0x1C     ACCEL_CONFIG     Accelerometer Configuration (AFS_SEL)
0x23     FIFO_EN          FIFO Enable
0x37     INT_PIN_CFG      Interrupt Pin Configuration
0x38     INT_ENABLE       Interrupt Enable
0x3A     INT_STATUS       Interrupt Status
0x3B-40  ACCEL_XOUT_H/L   Accelerometer X, Y, Z (6 bytes)
0x41-42  TEMP_OUT_H/L     Temperature (2 bytes)
0x43-48  GYRO_XOUT_H/L    Gyroscope X, Y, Z (6 bytes)
0x6A     USER_CTRL        User Control (FIFO_EN, FIFO_RST)
0x6B     PWR_MGMT_1       Power Management 1
0x6C     PWR_MGMT_2       Power Management 2
0x72-73  FIFO_COUNT_H/L   FIFO Count
0x74     FIFO_R_W         FIFO Read/Write
0x75     WHO_AM_I         Device ID (0x68)

┌─────────────────────────────────────────────────────────┐
│                    GYRO_CONFIG (0x1B)                   │
├─────┬─────┬───────────┬─────────────────────────────────┤
│ 7:5 │ 4:3 │    2:0    │                                 │
├─────┼─────┼───────────┼─────────────────────────────────┤
│ XG  │ FS  │  Reserved │                                 │
│ ST  │ SEL │           │                                 │
├─────┴─────┴───────────┴─────────────────────────────────┤
│  FS_SEL: 00=±250°/s, 01=±500, 10=±1000, 11=±2000        │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                   ACCEL_CONFIG (0x1C)                   │
├─────┬─────┬───────────┬─────────────────────────────────┤
│ 7:5 │ 4:3 │    2:0    │                                 │
├─────┼─────┼───────────┼─────────────────────────────────┤
│ XA  │ AFS │  Reserved │                                 │
│ ST  │ SEL │           │                                 │
├─────┴─────┴───────────┴─────────────────────────────────┤
│  AFS_SEL: 00=±2g, 01=±4g, 10=±8g, 11=±16g               │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                    PWR_MGMT_1 (0x6B)                    │
├─────┬─────┬─────┬─────┬─────┬───────────────────────────┤
│  7  │  6  │  5  │  4  │  3  │        2:0                │
├─────┼─────┼─────┼─────┼─────┼───────────────────────────┤
│ RST │SLEEP│CYCLE│ --- │TEMP │       CLKSEL              │
│     │     │     │     │ DIS │                           │
├─────┴─────┴─────┴─────┴─────┴───────────────────────────┤
│  CLKSEL: 001 = PLL with X Gyro reference (recommended)  │
└─────────────────────────────────────────────────────────┘

Data Register Layout (0x3B - 0x48):
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│ACCEL_X │ACCEL_Y │ACCEL_Z │  TEMP  │ GYRO_X │ GYRO_Y │ GYRO_Z │
│ H | L  │ H | L  │ H | L  │ H | L  │ H | L  │ H | L  │ H | L  │
├────────┴────────┴────────┴────────┴────────┴────────┴────────┤
│  14 bytes total, all big-endian 16-bit signed values         │
└──────────────────────────────────────────────────────────────┘
```

---

## 4. IIO Channel Structure

```mermaid
classDiagram
    class IIO_ACCEL_X {
        +type: IIO_ACCEL
        +channel2: IIO_MOD_X
        +scan_index: 0
        +info: RAW, CALIBBIAS, SCALE, SAMP_FREQ
    }

    class IIO_ACCEL_Y {
        +type: IIO_ACCEL
        +channel2: IIO_MOD_Y
        +scan_index: 1
    }

    class IIO_ACCEL_Z {
        +type: IIO_ACCEL
        +channel2: IIO_MOD_Z
        +scan_index: 2
    }

    class IIO_GYRO_X {
        +type: IIO_ANGL_VEL
        +channel2: IIO_MOD_X
        +scan_index: 3
    }

    class IIO_GYRO_Y {
        +type: IIO_ANGL_VEL
        +channel2: IIO_MOD_Y
        +scan_index: 4
    }

    class IIO_GYRO_Z {
        +type: IIO_ANGL_VEL
        +channel2: IIO_MOD_Z
        +scan_index: 5
    }

    class IIO_TEMP {
        +type: IIO_TEMP
        +scan_index: 6
        +info: RAW, SCALE, OFFSET
    }

    class IIO_TIMESTAMP {
        +type: IIO_TIMESTAMP
        +scan_index: 7
        +Soft timestamp
    }

    class mpu6050_channels {
        +8 channels total
        +3 accelerometer
        +3 gyroscope
        +1 temperature
        +1 timestamp
    }

    mpu6050_channels --> IIO_ACCEL_X
    mpu6050_channels --> IIO_ACCEL_Y
    mpu6050_channels --> IIO_ACCEL_Z
    mpu6050_channels --> IIO_GYRO_X
    mpu6050_channels --> IIO_GYRO_Y
    mpu6050_channels --> IIO_GYRO_Z
    mpu6050_channels --> IIO_TEMP
    mpu6050_channels --> IIO_TIMESTAMP
```

---

## 5. Sequence Diagram (Triggered Buffer Read)

```mermaid
sequenceDiagram
    participant HW as MPU6050 Hardware
    participant IRQ as IRQ Handler
    participant Trigger as IIO Trigger
    participant Handler as Trigger Handler
    participant IIO as IIO Core
    participant App as User Application

    HW->>IRQ: Data Ready IRQ
    IRQ->>Trigger: iio_trigger_poll()
    Trigger->>Handler: mpu6050_trigger_handler()

    alt FIFO Enabled
        Handler->>HW: Read FIFO_COUNT
        Handler->>HW: Read FIFO_R_W (14 bytes)
        Handler->>Handler: Parse FIFO data
    else Direct Read
        Handler->>HW: regmap_bulk_read(0x3B, 14)
        Note over Handler: Read all sensor data in burst
    end

    Handler->>Handler: Fill buffer structure
    Handler->>IIO: iio_push_to_buffers_with_timestamp()
    Handler->>Trigger: iio_trigger_notify_done()

    App->>IIO: read(/dev/iio:deviceX)
    IIO-->>App: Buffered sensor data
```

---

## 6. State Machine (Driver States)

```mermaid
stateDiagram-v2
    [*] --> Probing: i2c_driver probe

    Probing --> Initialized: mpu6050_hw_init()
    note right of Probing
        1. Allocate IIO device
        2. Setup regmap
        3. Request IRQ
        4. Register trigger
        5. Setup triggered buffer
    end note

    Initialized --> Configured: Set default config
    note right of Configured
        - Accel: ±2g
        - Gyro: ±250°/s
        - Sample rate: 100Hz
    end note

    Configured --> Registered: iio_device_register()

    Registered --> DirectMode: No trigger
    note right of DirectMode
        - Single shot reads
        - iio_device_claim_direct_mode()
    end note

    DirectMode --> BufferedMode: Trigger enabled
    note right of BufferedMode
        - Continuous sampling
        - IRQ-driven
        - FIFO optional
    end note

    BufferedMode --> DirectMode: Trigger disabled

    DirectMode --> Suspended: System suspend
    BufferedMode --> Suspended: System suspend

    Suspended --> DirectMode: System resume
    Suspended --> BufferedMode: System resume

    DirectMode --> Removed: Driver remove
    BufferedMode --> Removed: Driver remove
    Removed --> [*]
```

---

## 7. Activity Diagram (Hardware Initialization)

```mermaid
flowchart TD
    START([Start hw_init]) --> RESET[Write DEVICE_RESET to PWR_MGMT_1]
    RESET --> WAIT1[msleep 100ms]
    WAIT1 --> WAKE[Write CLKSEL_PLL_X to PWR_MGMT_1]
    WAKE --> WAIT2[msleep 10ms]

    WAIT2 --> READ_ID[Read WHO_AM_I register]
    READ_ID --> CHECK_ID{ID valid?}
    CHECK_ID -->|No| ERROR1[Return -ENODEV]
    CHECK_ID -->|Yes| LOG_ID[Log device ID]

    LOG_ID --> CONFIG[Write CONFIG register DLPF=3]
    CONFIG --> ACCEL[Set accel scale ±2g]
    ACCEL --> GYRO[Set gyro scale ±250°/s]
    GYRO --> RATE[Set sample rate 100Hz]

    RATE --> IRQ_CHECK{IRQ support?}
    IRQ_CHECK -->|Yes| INT_CFG[Configure INT_PIN_CFG]
    INT_CFG --> DONE
    IRQ_CHECK -->|No| DONE

    DONE --> SUCCESS([Return 0])
    ERROR1 --> FAIL([Return error])
```

---

## 8. Scale Tables and Conversions

```
Accelerometer Scale (m/s²):

┌────────────┬────────────┬───────────────┬────────────────────┐
│  AFS_SEL   │   Range    │ Sensitivity   │ Scale (µm/s²/LSB)  │
├────────────┼────────────┼───────────────┼────────────────────┤
│     0      │   ±2g      │  16384 LSB/g  │       598          │
│     1      │   ±4g      │   8192 LSB/g  │      1197          │
│     2      │   ±8g      │   4096 LSB/g  │      2394          │
│     3      │   ±16g     │   2048 LSB/g  │      4788          │
└────────────┴────────────┴───────────────┴────────────────────┘

Conversion: accel_m_s2 = raw_value × scale × 1e-6

Gyroscope Scale (rad/s):

┌────────────┬────────────┬───────────────┬────────────────────┐
│  FS_SEL    │   Range    │ Sensitivity   │ Scale (µrad/s/LSB) │
├────────────┼────────────┼───────────────┼────────────────────┤
│     0      │  ±250°/s   │  131 LSB/°/s  │       133          │
│     1      │  ±500°/s   │  65.5 LSB/°/s │       266          │
│     2      │  ±1000°/s  │  32.8 LSB/°/s │       532          │
│     3      │  ±2000°/s  │  16.4 LSB/°/s │      1065          │
└────────────┴────────────┴───────────────┴────────────────────┘

Conversion: gyro_rad_s = raw_value × scale × 1e-6

Temperature:
    Temp (°C) = (raw_value / 340.0) + 36.53
    Scale: 2941 µ°C/LSB
    Offset: 12420 (raw value for 0°C)

Sample Rate:
    Sample Rate = 1000 / (1 + SMPLRT_DIV) Hz
    SMPLRT_DIV = (1000 / desired_rate) - 1
```

---

## 9. FIFO Operation Flow

```mermaid
flowchart TD
    subgraph Enable["FIFO Enable"]
        E_START([Enable FIFO]) --> RESET[Write FIFO_RST to USER_CTRL]
        RESET --> CONFIG[Write FIFO_EN register]
        note right of CONFIG
            Enable sensors:
            - TEMP_FIFO_EN
            - XG/YG/ZG_FIFO_EN
            - ACCEL_FIFO_EN
        end note
        CONFIG --> ENABLE[Set FIFO_EN in USER_CTRL]
        ENABLE --> E_DONE([FIFO Running])
    end

    subgraph Read["FIFO Read"]
        R_START([Read FIFO]) --> COUNT[Read FIFO_COUNT_H/L]
        COUNT --> CHECK{Count >= 14?}
        CHECK -->|No| R_WAIT([Wait for data])
        CHECK -->|Yes| READ[Read FIFO_R_W × 14]
        READ --> PARSE[Parse sensor data]
        PARSE --> R_DONE([Return data])
    end

    subgraph Disable["FIFO Disable"]
        D_START([Disable FIFO]) --> CLR_EN[Clear FIFO_EN in USER_CTRL]
        CLR_EN --> CLR_CFG[Write 0 to FIFO_EN register]
        CLR_CFG --> D_DONE([FIFO Stopped])
    end
```

---

## 10. Sequence Diagram (read_raw Callback)

```mermaid
sequenceDiagram
    participant App as User Application
    participant Sysfs as IIO Sysfs
    participant IIO as IIO Core
    participant Driver as MPU6050 Driver
    participant Regmap as Regmap
    participant HW as MPU6050

    App->>Sysfs: cat in_accel_x_raw
    Sysfs->>IIO: Read attribute
    IIO->>Driver: mpu6050_read_raw(INFO_RAW)

    Driver->>IIO: iio_device_claim_direct_mode()
    IIO-->>Driver: Success

    alt Accelerometer channel
        Driver->>Driver: Determine axis from channel2
        Driver->>Regmap: regmap_bulk_read(ACCEL_XOUT_H + axis*2)
        Regmap->>HW: I2C read 2 bytes
        HW-->>Regmap: Raw data (BE)
        Regmap-->>Driver: Data
        Driver->>Driver: raw = (buf[0]<<8)|buf[1]
        Driver->>Driver: val = raw - accel_offset[axis]
    else Gyroscope channel
        Driver->>Regmap: regmap_bulk_read(GYRO_XOUT_H + axis*2)
        Regmap-->>Driver: Data
        Driver->>Driver: val = raw - gyro_offset[axis]
    else Temperature channel
        Driver->>Regmap: regmap_bulk_read(TEMP_OUT_H)
        Regmap-->>Driver: Data
        Driver->>Driver: val = raw
    end

    Driver->>IIO: iio_device_release_direct_mode()
    Driver-->>IIO: Return IIO_VAL_INT
    IIO-->>Sysfs: Format value
    Sysfs-->>App: Value string
```

---

## 11. Trigger Control State Machine

```mermaid
stateDiagram-v2
    [*] --> TriggerIdle: Trigger registered

    TriggerIdle --> TriggerEnabled: set_trigger_state(true)
    note right of TriggerEnabled
        1. Enable DATA_RDY interrupt
        2. Enable FIFO (if configured)
        3. irq_enabled = true
    end note

    TriggerEnabled --> DataReady: HW interrupt

    DataReady --> TriggerPoll: iio_trigger_poll()
    TriggerPoll --> HandlerRun: Trigger handler called
    HandlerRun --> PushData: Read sensor data
    PushData --> NotifyDone: iio_push_to_buffers
    NotifyDone --> TriggerEnabled: iio_trigger_notify_done()

    TriggerEnabled --> TriggerIdle: set_trigger_state(false)
    note left of TriggerIdle
        1. Disable interrupts
        2. Disable FIFO
        3. irq_enabled = false
    end note

    TriggerIdle --> [*]: Driver removed
```

---

## 12. Data Buffer Layout

```
Triggered Buffer Data Layout:

Scan Index:  0      1      2      3      4      5      6      7
           ┌──────┬──────┬──────┬──────┬──────┬──────┬──────┬──────────┐
           │ACC_X │ACC_Y │ACC_Z │GYR_X │GYR_Y │GYR_Z │ TEMP │TIMESTAMP │
           │ s16  │ s16  │ s16  │ s16  │ s16  │ s16  │ s16  │   s64    │
           │(BE)  │(BE)  │(BE)  │(BE)  │(BE)  │(BE)  │(BE)  │          │
           └──────┴──────┴──────┴──────┴──────┴──────┴──────┴──────────┘
Offset:     0      2      4      6      8      10     12     16
Size:       2B     2B     2B     2B     2B     2B     2B     8B

Total buffer size: 24 bytes (aligned to 8 bytes for timestamp)

struct buffer {
    s16 accel[3];       // Bytes 0-5
    s16 gyro[3];        // Bytes 6-11
    s16 temp;           // Bytes 12-13
    // 2 bytes padding  // Bytes 14-15
    s64 timestamp;      // Bytes 16-23 (8-byte aligned)
} __aligned(8);

scan_type for motion channels:
    .sign = 's'           // Signed
    .realbits = 16        // 16-bit resolution
    .storagebits = 16     // Stored in 16 bits
    .endianness = IIO_BE  // Big-endian from sensor
```

---

## 13. Power Management Flow

```mermaid
flowchart TD
    subgraph Suspend["System Suspend"]
        S_START([Suspend]) --> DISABLE_TRIG[Disable trigger]
        DISABLE_TRIG --> DISABLE_IRQ[Disable IRQ]
        DISABLE_IRQ --> SLEEP[Write SLEEP bit to PWR_MGMT_1]
        SLEEP --> S_DONE([Suspended])
    end

    subgraph Resume["System Resume"]
        R_START([Resume]) --> WAKE[Clear SLEEP bit]
        WAKE --> WAIT[msleep for stabilization]
        WAIT --> RESTORE[Restore configuration]
        RESTORE --> ENABLE_IRQ[Enable IRQ if was enabled]
        ENABLE_IRQ --> R_DONE([Resumed])
    end

    subgraph RuntimePM["Runtime PM"]
        PM_IDLE([Idle]) --> PM_SUSPEND[pm_runtime_suspend]
        PM_SUSPEND --> PM_LOW[Low power mode]
        PM_LOW --> PM_RESUME[pm_runtime_resume]
        PM_RESUME --> PM_ACTIVE[Full power]
        PM_ACTIVE --> PM_IDLE
    end
```

---

## 14. Kconfig Options

```mermaid
flowchart TD
    subgraph KConfig["Kconfig Options"]
        BASE[CONFIG_MPU6050<br/>Enable driver - tristate]

        BASE --> IRQ[CONFIG_MPU6050_IRQ<br/>Interrupt support]
        BASE --> DEBUG[CONFIG_MPU6050_DEBUG<br/>Debug output]

        IRQ --> TRIGGER[CONFIG_MPU6050_TRIGGER<br/>Hardware trigger]
        TRIGGER --> FIFO[CONFIG_MPU6050_FIFO<br/>FIFO buffering]

        BASE --> DMA[CONFIG_MPU6050_DMA<br/>DMA buffer support]
        DMA --> DMA_SIZE[CONFIG_MPU6050_DMA_BUFFER_SIZE<br/>Buffer size - int]
    end

    subgraph Features["Feature Matrix"]
        F_TABLE[
            Feature | Requires
            --------|----------
            Polled read | Base only
            Triggered buffer | IRQ + TRIGGER
            FIFO mode | IRQ + TRIGGER + FIFO
            DMA transfers | DMA
            Debug prints | DEBUG
        ]
    end
```

---

## 15. Device Tree Binding

```
Device Tree Example:

&i2c1 {
    status = "okay";

    mpu6050@68 {
        compatible = "invensense,mpu6050";
        reg = <0x68>;
        interrupt-parent = <&gpio>;
        interrupts = <25 IRQ_TYPE_EDGE_RISING>;
        mount-matrix = "1", "0", "0",
                       "0", "1", "0",
                       "0", "0", "1";
    };
};

┌─────────────────────────────────────────────────────────┐
│                    Device Tree Node                     │
├─────────────────────────────────────────────────────────┤
│  Parent: I2C bus controller node                        │
│  Compatible strings:                                    │
│    - "invensense,mpu6050"                               │
│    - "invensense,mpu6500"                               │
│    - "invensense,mpu9250"                               │
│                                                         │
│  ┌─────────────────┬───────────────────────────────┐    │
│  │   Property      │  Description                  │    │
│  ├─────────────────┼───────────────────────────────┤    │
│  │  reg            │  I2C address (0x68 or 0x69)   │    │
│  │  interrupts     │  Data ready IRQ              │    │
│  │  mount-matrix   │  Orientation matrix          │    │
│  └─────────────────┴───────────────────────────────┘    │
│                                                         │
│  WHO_AM_I Values:                                       │
│  - 0x68: MPU6050                                        │
│  - 0x70: MPU6500                                        │
│  - 0x71: MPU9250                                        │
└─────────────────────────────────────────────────────────┘
```

---

## 16. Userspace Interface

```
Sysfs Interface (/sys/bus/iio/devices/iio:deviceX/):

├── in_accel_x_raw           # Raw accelerometer X
├── in_accel_y_raw           # Raw accelerometer Y
├── in_accel_z_raw           # Raw accelerometer Z
├── in_accel_scale           # Current accelerometer scale
├── in_accel_scale_available # Available scales
├── in_accel_x_calibbias     # X axis offset
├── in_accel_y_calibbias     # Y axis offset
├── in_accel_z_calibbias     # Z axis offset
├── in_anglvel_x_raw         # Raw gyroscope X
├── in_anglvel_y_raw         # Raw gyroscope Y
├── in_anglvel_z_raw         # Raw gyroscope Z
├── in_anglvel_scale         # Current gyroscope scale
├── in_anglvel_scale_available
├── in_anglvel_x_calibbias
├── in_anglvel_y_calibbias
├── in_anglvel_z_calibbias
├── in_temp_raw              # Raw temperature
├── in_temp_scale            # Temperature scale
├── in_temp_offset           # Temperature offset
├── sampling_frequency       # Current sample rate
├── buffer/
│   ├── enable               # Enable/disable buffer
│   ├── length               # Buffer size
│   └── watermark            # Watermark level
├── trigger/
│   └── current_trigger      # Current trigger name
└── scan_elements/
    ├── in_accel_x_en        # Enable X accel in buffer
    ├── in_accel_x_index     # Scan index
    ├── in_accel_x_type      # Data format (le:s16/16>>0)
    └── ...

Character Device (/dev/iio:deviceX):
- Read to get buffered sensor data
- Use iio_buffer_read() in userspace
```

---

## Summary

The MPU6050 driver demonstrates:

1. **I2C Integration** - I2C client driver with regmap
2. **IIO Subsystem** - Standard sensor interface
3. **Multiple Channels** - Accelerometer, gyroscope, temperature
4. **Triggered Buffering** - IRQ-driven data collection
5. **FIFO Support** - Hardware buffering option
6. **DMA Support** - Optional DMA buffer allocation
7. **Calibration** - Offset and scale configuration
8. **Power Management** - Sleep mode and runtime PM
9. **Device Tree** - Flexible hardware configuration
10. **Regmap Caching** - Efficient register access
