# u-blox NEO-M8N GPS/GNSS Driver Tutorial

Complete guide to understanding the serial GPS driver using the GNSS subsystem.

## What This Driver Demonstrates

| Feature | Implementation | Purpose |
|---------|---------------|---------|
| GNSS Subsystem | `gnss_device` | Standard GPS interface |
| Serial (serdev) | `serdev_device_driver` | Modern serial API |
| UBX Protocol | Custom parser | Binary protocol handling |
| State Machine | Byte-by-byte parsing | Protocol decoding |
| Power Management | Regulator + GPIO | VCC and reset control |
| Completion API | ACK/NAK waiting | Command synchronization |

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER SPACE                                   │
│                                                                      │
│   gpsd ←────────────→ /dev/gnss0                                    │
│                       (read NMEA sentences)                         │
│                                                                      │
│   Custom app ←──────→ /dev/gnss0                                    │
│                       (read/write UBX/NMEA)                         │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       GNSS SUBSYSTEM                                 │
│                                                                      │
│   /dev/gnss0  ←──→  gnss_device                                     │
│                     - gnss_insert_raw() for incoming data           │
│                     - write_raw() for outgoing data                 │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    NEO-M8N DRIVER                                    │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    ublox_receive_buf()                       │   │
│  │                                                              │   │
│  │   Serial RX ──→ gnss_insert_raw() ──→ /dev/gnss0            │   │
│  │       │                                                      │   │
│  │       └──→ UBX Parser ──→ Handle ACK/NAK, NAV-PVT           │   │
│  └─────────────────────────────────────────────────────────────┘   │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │                    ublox_gnss_write_raw()                    │   │
│  │                                                              │   │
│  │   /dev/gnss0 write ──→ serdev_device_write_buf()            │   │
│  └─────────────────────────────────────────────────────────────┘   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    SERDEV SUBSYSTEM                                  │
│                                                                      │
│   serdev_device_write_buf() ──→ UART TX                             │
│   ublox_receive_buf() ←── UART RX                                   │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    NEO-M8N HARDWARE                                  │
│                                                                      │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │  UART Interface (9600-921600 baud)                          │   │
│   │                                                             │   │
│   │  Output: NMEA sentences (ASCII)                             │   │
│   │          $GPGGA,123519,4807.038,N,01131.000,E,1,...*47      │   │
│   │          UBX messages (binary)                              │   │
│   │                                                             │   │
│   │  Input:  UBX configuration commands                         │   │
│   └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## UBX Protocol Explained

```
┌───────────────────────────────────────────────────────────────────┐
│                     UBX MESSAGE FORMAT                             │
├───────────────────────────────────────────────────────────────────┤
│                                                                    │
│   Byte:   0      1      2      3      4      5       6...     N-1 N│
│          ┌──────┬──────┬──────┬──────┬──────┬──────┬───────┬─────┐│
│          │ 0xB5 │ 0x62 │Class │  ID  │Len Lo│Len Hi│Payload│CK_A ││
│          │      │      │      │      │      │      │       │CK_B ││
│          └──────┴──────┴──────┴──────┴──────┴──────┴───────┴─────┘│
│          │← Sync chars─→│                  │←length bytes→│      │
│                                                                    │
│   Checksum:  8-bit Fletcher checksum over Class..Payload          │
│              CK_A = running sum                                    │
│              CK_B = running sum of CK_A                           │
│                                                                    │
│   Example: UBX-CFG-MSG (configure message rate)                   │
│   B5 62 06 01 03 00 01 07 01 13 51                               │
│   │  │  │  │  │  │  │  │  │  └──┴── Checksum                     │
│   │  │  │  │  │  │  │  │  └─────── Rate (1 = every measurement)  │
│   │  │  │  │  │  │  │  └────────── MSG ID (NAV-PVT = 0x07)       │
│   │  │  │  │  │  │  └───────────── MSG Class (NAV = 0x01)        │
│   │  │  │  │  │  └──────────────── Length = 3                    │
│   │  │  │  │  └─────────────────── ID (CFG-MSG = 0x01)           │
│   │  │  │  └────────────────────── Class (CFG = 0x06)            │
│   │  │  └───────────────────────── Sync char 2                   │
│   │  └──────────────────────────── Sync char 1                   │
│                                                                    │
└───────────────────────────────────────────────────────────────────┘
```

---

## Code Walkthrough

### Part 1: UBX Protocol Constants (Lines 36-68)

```c
/* UBX Protocol Sync Characters */
#define UBX_SYNC1               0xB5
#define UBX_SYNC2               0x62

/* Message Classes */
#define UBX_CLASS_NAV           0x01    /* Navigation Results */
#define UBX_CLASS_ACK           0x05    /* Acknowledgements */
#define UBX_CLASS_CFG           0x06    /* Configuration */

/* CFG Message IDs */
#define UBX_CFG_PRT             0x00    /* Port configuration */
#define UBX_CFG_MSG             0x01    /* Message rate configuration */
#define UBX_CFG_RATE            0x08    /* Navigation rate */

/* NAV Message IDs */
#define UBX_NAV_PVT             0x07    /* Position, Velocity, Time */
```

### Part 2: State Machine Parser (Lines 316-395)

```c
static void ublox_parse_ubx_byte(struct ublox_neo_m8n *priv, u8 byte)
{
    switch (priv->ubx_state) {
    case UBX_WAIT_SYNC1:
        if (byte == UBX_SYNC1)
            priv->ubx_state = UBX_WAIT_SYNC2;
        break;

    case UBX_WAIT_SYNC2:
        if (byte == UBX_SYNC2)
            priv->ubx_state = UBX_WAIT_CLASS;
        else
            priv->ubx_state = UBX_WAIT_SYNC1;  /* Reset on mismatch */
        break;

    case UBX_WAIT_CLASS:
        priv->ubx_class = byte;
        priv->ubx_ck_a = byte;      /* Start checksum */
        priv->ubx_ck_b = byte;
        priv->ubx_state = UBX_WAIT_ID;
        break;

    /* ... continue through all states ... */

    case UBX_WAIT_CK_B:
        if (byte == priv->ubx_ck_b) {
            ublox_handle_ubx_message(priv);  /* Valid message! */
        } else {
            priv->errors++;                   /* Checksum failed */
        }
        priv->ubx_state = UBX_WAIT_SYNC1;
        break;
    }
}
```

**Why a state machine?**
- Serial data arrives byte-by-byte
- Must handle partial messages, noise, interleaved NMEA
- Robust against corrupted data

### Part 3: Serial Receive Callback (Lines 402-429)

```c
static size_t ublox_receive_buf(struct serdev_device *serdev,
                                const u8 *buf, size_t len)
{
    struct ublox_neo_m8n *priv = serdev_device_get_drvdata(serdev);

    /* 1. Forward ALL data to GNSS subsystem (for NMEA) */
    gnss_insert_raw(priv->gdev, buf, len);

    /* 2. Also parse for UBX messages (for ACK handling) */
    for (i = 0; i < len; i++) {
        if (buf[i] == UBX_SYNC1 || priv->ubx_state != UBX_WAIT_SYNC1) {
            ublox_parse_ubx_byte(priv, buf[i]);
        }

        /* Count NMEA sentences */
        if (buf[i] == '$')
            priv->nmea_count++;
    }

    return len;  /* Tell serdev we consumed all bytes */
}
```

**Dual-path processing:**
1. `gnss_insert_raw()` → makes data available on `/dev/gnss0`
2. UBX parser → handles ACK/NAK for configuration commands

### Part 4: Checksum Calculation (Lines 192-201)

```c
static void ubx_checksum(const u8 *data, size_t len, u8 *ck_a, u8 *ck_b)
{
    *ck_a = 0;
    *ck_b = 0;

    while (len--) {
        *ck_a += *data++;    /* Sum of bytes */
        *ck_b += *ck_a;      /* Sum of sums */
    }
}
```

**Fletcher checksum properties:**
- Simple to compute
- Catches byte transposition errors
- Two bytes provide good error detection

### Part 5: Command with ACK Waiting (Lines 252-278)

```c
static int ublox_send_ubx_wait_ack(struct ublox_neo_m8n *priv,
                                   u8 msg_class, u8 msg_id,
                                   const u8 *payload, u16 payload_len)
{
    /* Prepare for ACK */
    reinit_completion(&priv->ack_completion);
    priv->ack_received = false;

    /* Send command */
    ret = ublox_send_ubx(priv, msg_class, msg_id, payload, payload_len);
    if (ret < 0)
        return ret;

    /* Wait for ACK-ACK or ACK-NAK */
    if (!wait_for_completion_timeout(&priv->ack_completion,
                                     msecs_to_jiffies(1000))) {
        return -ETIMEDOUT;
    }

    return priv->ack_received ? 0 : -EPROTO;
}
```

**Why wait for ACK?**
- Ensures configuration was accepted
- Some settings require specific conditions
- NAK indicates invalid command

### Part 6: GNSS Device Registration (Lines 645-652)

```c
/* Allocate GNSS device */
priv->gdev = gnss_allocate_device(dev);
priv->gdev->type = GNSS_TYPE_UBX;     /* u-blox device */
priv->gdev->ops = &ublox_gnss_ops;
gnss_set_drvdata(priv->gdev, priv);

/* Register with GNSS subsystem */
ret = gnss_register_device(priv->gdev);
/* Creates /dev/gnss0 */
```

### Part 7: GNSS Operations (Lines 442-468)

```c
static int ublox_gnss_open(struct gnss_device *gdev)
{
    struct ublox_neo_m8n *priv = gnss_get_drvdata(gdev);
    return serdev_device_open(priv->serdev);
}

static int ublox_gnss_write_raw(struct gnss_device *gdev,
                                const unsigned char *buf, size_t len)
{
    struct ublox_neo_m8n *priv = gnss_get_drvdata(gdev);
    return serdev_device_write_buf(priv->serdev, buf, len);
}

static const struct gnss_operations ublox_gnss_ops = {
    .open = ublox_gnss_open,
    .close = ublox_gnss_close,
    .write_raw = ublox_gnss_write_raw,
};
```

---

## Device Tree Configuration

```dts
&uart1 {
    status = "okay";

    gnss: gps {
        compatible = "u-blox,neo-m8n";

        /* Power control */
        vcc-supply = <&reg_gps_3v3>;

        /* Optional: hardware reset */
        reset-gpios = <&gpio 4 GPIO_ACTIVE_LOW>;

        /* Default baud: 9600, can be changed via UBX-CFG-PRT */
    };
};
```

---

## Userspace Usage

### Using gpsd
```bash
# Install gpsd
sudo apt install gpsd gpsd-clients

# Configure gpsd
sudo systemctl stop gpsd
sudo gpsd /dev/gnss0 -F /var/run/gpsd.sock

# Monitor with cgps
cgps -s

# Or gpsmon for raw data
gpsmon
```

### Direct Access
```bash
# Read NMEA sentences
cat /dev/gnss0
# $GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,...*47
# $GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,...*6A

# Send UBX command (example: poll NAV-PVT)
# B5 62 01 07 00 00 08 19
echo -ne '\xB5\x62\x01\x07\x00\x00\x08\x19' > /dev/gnss0
```

### NMEA Sentence Format

```
$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,47.0,M,,*47
│     │      │        │ │         │ │ │  │   │      │ │    │ │ └─ Checksum
│     │      │        │ │         │ │ │  │   │      │ │    │ └─── DGPS ref
│     │      │        │ │         │ │ │  │   │      │ │    └───── DGPS age
│     │      │        │ │         │ │ │  │   │      │ └────────── Geoid sep M
│     │      │        │ │         │ │ │  │   │      └──────────── Altitude M
│     │      │        │ │         │ │ │  │   └─────────────────── Altitude
│     │      │        │ │         │ │ │  └─────────────────────── HDOP
│     │      │        │ │         │ │ └────────────────────────── Satellites
│     │      │        │ │         │ └──────────────────────────── Fix quality
│     │      │        │ │         └────────────────────────────── Longitude E
│     │      │        │ └──────────────────────────────────────── Longitude
│     │      │        └────────────────────────────────────────── Latitude N
│     │      └─────────────────────────────────────────────────── Latitude
│     └────────────────────────────────────────────────────────── UTC Time
└──────────────────────────────────────────────────────────────── Sentence ID
```

---

## Key Learning Points

### 1. Serdev vs TTY
```c
/* Old way: TTY line discipline */
/* Complex, requires userspace setup */

/* New way: serdev */
/* Direct kernel binding to serial device */
serdev_device_write_buf(serdev, buf, len);  /* Simple! */
```

### 2. GNSS Subsystem Benefits
- Standard `/dev/gnssX` interface
- Works with gpsd out of the box
- Handles multiple GPS types (UBX, SIRF, MTK)

### 3. State Machine Pattern
- Essential for parsing streaming protocols
- Handle corruption gracefully
- Easy to extend for new message types

### 4. Completion for Command/Response
```c
/* Thread A: Send command */
reinit_completion(&comp);
send_command();
wait_for_completion_timeout(&comp, timeout);

/* Thread B (IRQ/callback): Got response */
complete(&comp);  /* Wake Thread A */
```

---

## Power Management

```c
static int ublox_suspend(struct device *dev)
{
    /* Option 1: Full power off */
    ublox_power_off(priv);

    /* Option 2: UBX backup mode (faster cold start) */
    /* Send UBX-RXM-PMREQ message */
}

static int ublox_resume(struct device *dev)
{
    ublox_power_on(priv);
    ublox_configure_device(priv);  /* Reconfigure */
}
```

---

## Exercises

1. **Add position parsing**: Parse UBX-NAV-PVT and expose via sysfs
2. **Add DGPS/RTK**: Implement RTCM injection for cm-accuracy
3. **Add geofence**: Use UBX-CFG-GEOFENCE for location alerts
4. **Add time sync**: Implement PPS (pulse-per-second) handling
5. **Add multiple GNSS**: Configure GPS+GLONASS+Galileo
