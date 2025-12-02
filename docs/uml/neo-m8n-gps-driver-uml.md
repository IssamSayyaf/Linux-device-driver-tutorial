# u-blox NEO-M8N GPS/GNSS Driver - UML Documentation

## Overview

The NEO-M8N driver is a Serial Device Driver that integrates with the Linux GNSS subsystem to provide GPS/GLONASS/Galileo/BeiDou positioning functionality.

**Driver Type:** Serial Device (serdev) GNSS Driver
**File:** `drivers/gps/neo_m8n/ublox_neo_m8n.c`
**Subsystems:** GNSS, serdev, Regulator, GPIO, PM Runtime

---

## 1. Class Diagram (Data Structures)

```mermaid
classDiagram
    class ublox_neo_m8n {
        +struct serdev_device *serdev
        +struct gnss_device *gdev
        +struct regulator *vcc
        +struct gpio_desc *reset_gpio
        +struct gpio_desc *extint_gpio
        +speed_t current_baud
        +bool configured
        +u8 rx_buffer[2048]
        +size_t rx_len
        +enum ubx_state ubx_state
        +u8 ubx_class
        +u8 ubx_id
        +u16 ubx_len
        +u16 ubx_idx
        +u8 ubx_ck_a
        +u8 ubx_ck_b
        +u8 ubx_payload[512]
        +struct completion ack_completion
        +bool ack_received
        +u8 ack_class
        +u8 ack_id
        +unsigned long nmea_count
        +unsigned long ubx_count
        +unsigned long errors
    }

    class ubx_header {
        +u8 sync1
        +u8 sync2
        +u8 msg_class
        +u8 msg_id
        +u16 length
    }

    class ubx_nav_pvt {
        +u32 itow
        +u16 year
        +u8 month, day, hour, min, sec
        +u8 valid, fix_type, flags
        +u8 num_sv
        +s32 lon, lat
        +s32 height, hmsl
        +u32 hacc, vacc
        +s32 veln, vele, veld
        +s32 gspeed
        +u16 pdop
    }

    class gnss_device {
        +enum gnss_type type
        +struct gnss_operations *ops
        +void *priv
    }

    class gnss_operations {
        +open()
        +close()
        +write_raw()
    }

    class serdev_device_ops {
        +receive_buf()
        +write_wakeup()
    }

    class serdev_device_driver {
        +probe()
        +remove()
        +driver
    }

    ublox_neo_m8n --> serdev_device : uses
    ublox_neo_m8n --> gnss_device : manages
    gnss_device --> gnss_operations : ops
    ublox_neo_m8n --> ubx_header : parses
    ublox_neo_m8n --> ubx_nav_pvt : decodes
    serdev_device_driver --> ublox_neo_m8n : creates
```

---

## 2. Component Diagram (System Architecture)

```mermaid
flowchart TB
    subgraph UserSpace["User Space"]
        GPS_APP[GPS Application]
        GNSS_DEV["/dev/gnss0"]
    end

    subgraph KernelSpace["Kernel Space"]
        subgraph GNSS["GNSS Subsystem"]
            GNSS_CORE[GNSS Core]
            GNSS_CDEV[GNSS Char Device]
        end

        subgraph Driver["NEO-M8N Driver"]
            PROBE[Probe Function]
            SERDEV_OPS[Serdev Callbacks]
            UBX_PARSER[UBX Protocol Parser]
            GNSS_OPS[GNSS Operations]
            PWR_MGMT[Power Management]
        end

        subgraph SerDev["Serial Device Bus"]
            SERDEV_CORE[Serdev Core]
            UART_LAYER[UART Driver]
        end

        subgraph Power["Power Subsystem"]
            REGULATOR[Regulator Framework]
            GPIO[GPIO Controller]
        end
    end

    subgraph Hardware["Hardware"]
        NEO_M8N[u-blox NEO-M8N]
        UART_HW[UART Interface]
        VCC[Power Supply]
        RESET[Reset Line]
    end

    GPS_APP --> GNSS_DEV
    GNSS_DEV --> GNSS_CDEV
    GNSS_CDEV --> GNSS_CORE
    GNSS_CORE --> GNSS_OPS
    GNSS_OPS --> SERDEV_OPS
    SERDEV_OPS --> UBX_PARSER
    SERDEV_OPS --> SERDEV_CORE
    SERDEV_CORE --> UART_LAYER
    PWR_MGMT --> REGULATOR
    PWR_MGMT --> GPIO
    UART_LAYER --> UART_HW
    REGULATOR --> VCC
    GPIO --> RESET
    UART_HW --> NEO_M8N
    VCC --> NEO_M8N
    RESET --> NEO_M8N
```

---

## 3. UBX Protocol State Machine

```mermaid
stateDiagram-v2
    [*] --> WAIT_SYNC1: Start/Reset

    WAIT_SYNC1 --> WAIT_SYNC2: byte == 0xB5
    WAIT_SYNC1 --> WAIT_SYNC1: byte != 0xB5

    WAIT_SYNC2 --> WAIT_CLASS: byte == 0x62
    WAIT_SYNC2 --> WAIT_SYNC1: byte != 0x62

    WAIT_CLASS --> WAIT_ID: Store class, Init checksum
    note right of WAIT_CLASS
        ubx_class = byte
        ck_a = byte
        ck_b = byte
    end note

    WAIT_ID --> WAIT_LEN1: Store ID, Update checksum
    note right of WAIT_ID
        ubx_id = byte
        ck_a += byte
        ck_b += ck_a
    end note

    WAIT_LEN1 --> WAIT_LEN2: Store low byte of length

    WAIT_LEN2 --> WAIT_PAYLOAD: len > 0
    WAIT_LEN2 --> WAIT_CK_A: len == 0
    WAIT_LEN2 --> WAIT_SYNC1: len > 512 (overflow)

    WAIT_PAYLOAD --> WAIT_PAYLOAD: idx < len
    WAIT_PAYLOAD --> WAIT_CK_A: idx >= len
    note right of WAIT_PAYLOAD
        payload[idx++] = byte
        ck_a += byte
        ck_b += ck_a
    end note

    WAIT_CK_A --> WAIT_CK_B: byte == ck_a
    WAIT_CK_A --> WAIT_SYNC1: Checksum error

    WAIT_CK_B --> HandleMessage: byte == ck_b
    WAIT_CK_B --> WAIT_SYNC1: Checksum error

    HandleMessage --> WAIT_SYNC1: Process complete
```

---

## 4. Sequence Diagram (Device Initialization)

```mermaid
sequenceDiagram
    participant DT as Device Tree
    participant Serdev as Serdev Core
    participant Driver as NEO-M8N Driver
    participant GNSS as GNSS Core
    participant Reg as Regulator
    participant GPIO as GPIO

    DT->>Serdev: Device binding
    Serdev->>Driver: ublox_neo_m8n_probe()

    Driver->>Driver: Allocate private data
    Driver->>Driver: Init completion

    Driver->>Reg: devm_regulator_get_optional()
    Reg-->>Driver: VCC regulator

    Driver->>GPIO: devm_gpiod_get_optional(reset)
    GPIO-->>Driver: Reset GPIO

    Driver->>GPIO: devm_gpiod_get_optional(extint)
    GPIO-->>Driver: EXTINT GPIO

    Driver->>GNSS: gnss_allocate_device()
    GNSS-->>Driver: GNSS device

    Driver->>Driver: Set gdev->type = GNSS_TYPE_UBX
    Driver->>Driver: Set gdev->ops = &ublox_gnss_ops

    Driver->>Driver: ublox_power_on()
    activate Driver
    Driver->>Reg: regulator_enable()
    Driver->>GPIO: gpiod_set_value(reset, 0)
    Driver->>Driver: msleep(100)
    deactivate Driver

    Driver->>Serdev: serdev_device_open()
    Driver->>Serdev: serdev_device_set_client_ops()
    Driver->>Serdev: serdev_device_set_baudrate(9600)
    Driver->>Serdev: serdev_device_set_flow_control(false)

    Driver->>GNSS: gnss_register_device()
    GNSS-->>Driver: Success

    Driver->>Driver: ublox_configure_device()
    Note over Driver: Enable NAV-PVT, Set rate

    Driver-->>Serdev: Return 0
```

---

## 5. Sequence Diagram (Data Reception)

```mermaid
sequenceDiagram
    participant HW as NEO-M8N Hardware
    participant UART as UART Driver
    participant Serdev as Serdev Core
    participant Driver as NEO-M8N Driver
    participant GNSS as GNSS Core
    participant App as User Application

    HW->>UART: Serial data (NMEA/UBX)
    UART->>Serdev: Data received
    Serdev->>Driver: ublox_receive_buf(buf, len)

    Driver->>GNSS: gnss_insert_raw(gdev, buf, len)
    Note over GNSS: Forward to /dev/gnss0

    loop For each byte
        Driver->>Driver: Check for '$' (NMEA start)
        alt byte == '$'
            Driver->>Driver: nmea_count++
        end

        Driver->>Driver: Check for UBX sync
        alt byte == 0xB5 or parsing UBX
            Driver->>Driver: ublox_parse_ubx_byte()
        end
    end

    alt Complete UBX message received
        Driver->>Driver: ublox_handle_ubx_message()

        alt ACK message
            Driver->>Driver: Set ack_received
            Driver->>Driver: complete(&ack_completion)
        else NAV-PVT message
            Driver->>Driver: Parse position data
        end
    end

    Driver-->>Serdev: Return len (bytes consumed)

    App->>GNSS: read(/dev/gnss0)
    GNSS-->>App: NMEA/UBX data
```

---

## 6. Activity Diagram (UBX Message Transmission)

```mermaid
flowchart TD
    START([Start Send UBX]) --> BUILD[Build UBX Message]

    BUILD --> SET_SYNC[Set sync bytes 0xB5, 0x62]
    SET_SYNC --> SET_CLASS[Set message class]
    SET_CLASS --> SET_ID[Set message ID]
    SET_ID --> SET_LEN[Set payload length]
    SET_LEN --> COPY_PAYLOAD[Copy payload data]

    COPY_PAYLOAD --> CALC_CK[Calculate checksum]
    note right of CALC_CK
        ck_a = ck_b = 0
        for each byte in class..payload:
            ck_a += byte
            ck_b += ck_a
    end note

    CALC_CK --> APPEND_CK[Append ck_a, ck_b]
    APPEND_CK --> SEND[serdev_device_write_buf()]

    SEND --> WAIT_ACK{Wait for ACK?}
    WAIT_ACK -->|No| DONE1([Return])

    WAIT_ACK -->|Yes| REINIT[reinit_completion()]
    REINIT --> WAIT[wait_for_completion_timeout()]

    WAIT --> TIMEOUT{Timeout?}
    TIMEOUT -->|Yes| RET_TIMEOUT[Return -ETIMEDOUT]

    TIMEOUT -->|No| CHECK_ACK{ACK received?}
    CHECK_ACK -->|Yes| RET_SUCCESS[Return 0]
    CHECK_ACK -->|No| RET_PROTO[Return -EPROTO]
```

---

## 7. Power Management State Diagram

```mermaid
stateDiagram-v2
    [*] --> PowerOff: Initial State

    PowerOff --> PoweringOn: probe() called
    note right of PoweringOn
        1. regulator_enable(vcc)
        2. gpiod_set_value(reset, 0)
        3. msleep(100)
    end note

    PoweringOn --> Active: Power stable

    Active --> Configured: Configuration sent
    note right of Configured
        - NAV-PVT enabled
        - Rate set to 1Hz
        - UART configured
    end note

    Configured --> Active: Operating normally

    Active --> Suspended: System suspend
    note right of Suspended
        1. Power off device
        2. Disable regulator
    end note

    Suspended --> PoweringOn: System resume

    Active --> PowerOff: remove() called
    note left of PowerOff
        1. gnss_deregister_device()
        2. serdev_device_close()
        3. ublox_power_off()
    end note

    PowerOff --> [*]
```

---

## 8. UBX Message Class Hierarchy

```mermaid
classDiagram
    class UBX_Message {
        +sync1: 0xB5
        +sync2: 0x62
        +class: u8
        +id: u8
        +length: u16
        +payload: u8[]
        +ck_a: u8
        +ck_b: u8
    }

    class UBX_NAV {
        +class: 0x01
        Navigation Results
    }

    class UBX_RXM {
        +class: 0x02
        Receiver Manager
    }

    class UBX_INF {
        +class: 0x04
        Information
    }

    class UBX_ACK {
        +class: 0x05
        Acknowledgement
    }

    class UBX_CFG {
        +class: 0x06
        Configuration
    }

    class UBX_MON {
        +class: 0x0A
        Monitoring
    }

    class NAV_PVT {
        +id: 0x07
        +itow: GPS time
        +lon, lat: Position
        +height: Altitude
        +fix_type: Fix status
        +num_sv: Satellites
    }

    class NAV_POSLLH {
        +id: 0x02
        Geodetic Position
    }

    class NAV_STATUS {
        +id: 0x03
        Receiver Status
    }

    class CFG_PRT {
        +id: 0x00
        Port Configuration
    }

    class CFG_RATE {
        +id: 0x08
        Navigation Rate
    }

    class CFG_MSG {
        +id: 0x01
        Message Configuration
    }

    class ACK_ACK {
        +id: 0x01
        Message Acknowledged
    }

    class ACK_NAK {
        +id: 0x00
        Message Not Acknowledged
    }

    UBX_Message <|-- UBX_NAV
    UBX_Message <|-- UBX_RXM
    UBX_Message <|-- UBX_INF
    UBX_Message <|-- UBX_ACK
    UBX_Message <|-- UBX_CFG
    UBX_Message <|-- UBX_MON

    UBX_NAV <|-- NAV_PVT
    UBX_NAV <|-- NAV_POSLLH
    UBX_NAV <|-- NAV_STATUS

    UBX_CFG <|-- CFG_PRT
    UBX_CFG <|-- CFG_RATE
    UBX_CFG <|-- CFG_MSG

    UBX_ACK <|-- ACK_ACK
    UBX_ACK <|-- ACK_NAK
```

---

## 9. UBX Frame Format

```
UBX Protocol Frame Structure:

┌──────┬──────┬───────┬────┬────────┬─────────────────┬──────┬──────┐
│ Sync │ Sync │ Class │ ID │ Length │    Payload      │ CK_A │ CK_B │
│  1   │  2   │       │    │(16-bit)│   (variable)    │      │      │
├──────┼──────┼───────┼────┼────────┼─────────────────┼──────┼──────┤
│ 0xB5 │ 0x62 │  1B   │ 1B │   2B   │   0-N bytes     │  1B  │  1B  │
└──────┴──────┴───────┴────┴────────┴─────────────────┴──────┴──────┘
         │              │                                │
         └──────────────┴─── Checksum calculated over ───┘
                             Class + ID + Length + Payload

Checksum Algorithm (Fletcher-8):
    ck_a = 0, ck_b = 0
    for byte in [class, id, length_lo, length_hi, payload...]:
        ck_a = (ck_a + byte) & 0xFF
        ck_b = (ck_b + ck_a) & 0xFF
```

---

## 10. NAV-PVT Message Structure

```mermaid
classDiagram
    class NAV_PVT_Payload {
        +u32 iTOW : GPS time of week (ms)
        +u16 year : Year (UTC)
        +u8 month : Month (1-12)
        +u8 day : Day (1-31)
        +u8 hour : Hour (0-23)
        +u8 min : Minute (0-59)
        +u8 sec : Second (0-60)
        +u8 valid : Validity flags
        +u32 tAcc : Time accuracy (ns)
        +s32 nano : Nanoseconds (-1e9..1e9)
        +u8 fixType : Fix type (0-5)
        +u8 flags : Fix status flags
        +u8 flags2 : Additional flags
        +u8 numSV : Number of satellites
        +s32 lon : Longitude (1e-7 deg)
        +s32 lat : Latitude (1e-7 deg)
        +s32 height : Height above ellipsoid (mm)
        +s32 hMSL : Height above MSL (mm)
        +u32 hAcc : Horizontal accuracy (mm)
        +u32 vAcc : Vertical accuracy (mm)
        +s32 velN : NED north velocity (mm/s)
        +s32 velE : NED east velocity (mm/s)
        +s32 velD : NED down velocity (mm/s)
        +s32 gSpeed : Ground speed (mm/s)
        +s32 headMot : Heading of motion (1e-5 deg)
        +u32 sAcc : Speed accuracy (mm/s)
        +u32 headAcc : Heading accuracy (1e-5 deg)
        +u16 pDOP : Position DOP
        +s32 headVeh : Vehicle heading
        +s16 magDec : Magnetic declination
        +u16 magAcc : Magnetic declination accuracy
    }
```

---

## 11. Data Flow Through Driver

```mermaid
flowchart LR
    subgraph Satellite
        GPS[GPS Satellites]
        GLO[GLONASS]
        GAL[Galileo]
        BDS[BeiDou]
    end

    subgraph Hardware
        ANT[Antenna]
        RX[NEO-M8N Receiver]
    end

    subgraph Driver
        SERDEV[Serial Receive]
        PARSER[Protocol Parser]
        GNSS_IF[GNSS Interface]
    end

    subgraph Output
        NMEA_OUT[NMEA Sentences]
        UBX_OUT[UBX Binary]
        USER[User Application]
    end

    GPS --> ANT
    GLO --> ANT
    GAL --> ANT
    BDS --> ANT
    ANT --> RX
    RX -->|UART| SERDEV
    SERDEV --> PARSER
    PARSER -->|NMEA| NMEA_OUT
    PARSER -->|UBX| UBX_OUT
    NMEA_OUT --> GNSS_IF
    UBX_OUT --> GNSS_IF
    GNSS_IF -->|/dev/gnss0| USER
```

---

## 12. Device Tree Binding

```
Device Tree Example:

&uart1 {
    status = "okay";

    gnss: gnss@0 {
        compatible = "u-blox,neo-m8n";
        vcc-supply = <&reg_3v3>;
        reset-gpios = <&gpio 5 GPIO_ACTIVE_LOW>;
        extint-gpios = <&gpio 6 GPIO_ACTIVE_HIGH>;
    };
};

┌─────────────────────────────────────────────────────┐
│                  Device Tree Node                   │
├─────────────────────────────────────────────────────┤
│  Parent: UART controller node                       │
│  Compatible strings:                                │
│    - "u-blox,neo-m8"                                │
│    - "u-blox,neo-m8n"                               │
│    - "u-blox,neo-m8m"                               │
│    - "u-blox,neo-m8q"                               │
│    - "u-blox,neo-m9n"                               │
│                                                     │
│  Properties:                                        │
│  ┌────────────────┬───────────────────────────┐     │
│  │  vcc-supply    │ Regulator for power       │     │
│  │  reset-gpios   │ Reset control (optional)  │     │
│  │  extint-gpios  │ External interrupt (opt)  │     │
│  └────────────────┴───────────────────────────┘     │
└─────────────────────────────────────────────────────┘
```

---

## 13. GNSS Operations Interface

```mermaid
classDiagram
    class gnss_operations {
        <<interface>>
        +open(gdev) int
        +close(gdev) void
        +write_raw(gdev, buf, len) int
    }

    class ublox_gnss_ops {
        +open: ublox_gnss_open()
        +close: ublox_gnss_close()
        +write_raw: ublox_gnss_write_raw()
    }

    class ublox_gnss_open {
        Opens serial port
        serdev_device_open()
    }

    class ublox_gnss_close {
        Closes serial port
        serdev_device_close()
    }

    class ublox_gnss_write_raw {
        Writes to device
        serdev_device_write_buf()
    }

    gnss_operations <|.. ublox_gnss_ops
    ublox_gnss_ops --> ublox_gnss_open
    ublox_gnss_ops --> ublox_gnss_close
    ublox_gnss_ops --> ublox_gnss_write_raw
```

---

## Summary

The NEO-M8N driver demonstrates:

1. **GNSS Subsystem Integration** - Standard kernel GNSS interface
2. **Serial Device (serdev)** - Modern serial port abstraction
3. **UBX Protocol Parsing** - Binary protocol state machine
4. **Power Management** - Regulator and GPIO control
5. **Multi-Protocol Support** - NMEA and UBX simultaneous handling
6. **ACK/NAK Handling** - Synchronous command confirmation
7. **Device Tree Configuration** - Flexible hardware binding
8. **Multi-Constellation Support** - GPS, GLONASS, Galileo, BeiDou
