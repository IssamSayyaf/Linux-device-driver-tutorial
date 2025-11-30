/*
 * ublox_neo_m8n.c - u-blox NEO-M8N GPS/GNSS Driver
 *
 * This driver demonstrates:
 * - GNSS subsystem integration
 * - Serial/UART device communication
 * - NMEA/UBX protocol handling
 * - Character device interface
 * - Power management
 *
 * The NEO-M8N supports:
 * - GPS, GLONASS, Galileo, BeiDou
 * - UART, I2C, SPI interfaces
 * - UBX and NMEA protocols
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/serdev.h>
#include <linux/gnss.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>

#define DRIVER_NAME "ublox-neo-m8n"

/* UBX Protocol Constants */
#define UBX_SYNC1               0xB5
#define UBX_SYNC2               0x62

/* UBX Message Classes */
#define UBX_CLASS_NAV           0x01    /* Navigation */
#define UBX_CLASS_RXM           0x02    /* Receiver Manager */
#define UBX_CLASS_INF           0x04    /* Information */
#define UBX_CLASS_ACK           0x05    /* Ack/Nak */
#define UBX_CLASS_CFG           0x06    /* Configuration */
#define UBX_CLASS_UPD           0x09    /* Firmware Update */
#define UBX_CLASS_MON           0x0A    /* Monitoring */
#define UBX_CLASS_AID           0x0B    /* AssistNow Aiding */
#define UBX_CLASS_TIM           0x0D    /* Timing */
#define UBX_CLASS_ESF           0x10    /* External Sensor Fusion */
#define UBX_CLASS_MGA           0x13    /* GNSS Assistance */
#define UBX_CLASS_LOG           0x21    /* Logging */
#define UBX_CLASS_SEC           0x27    /* Security */
#define UBX_CLASS_HNR           0x28    /* High Rate Navigation */

/* UBX-CFG Messages */
#define UBX_CFG_PRT             0x00    /* Port configuration */
#define UBX_CFG_MSG             0x01    /* Message configuration */
#define UBX_CFG_RST             0x04    /* Reset receiver */
#define UBX_CFG_RATE            0x08    /* Navigation rate */
#define UBX_CFG_NAV5            0x24    /* Navigation engine settings */
#define UBX_CFG_GNSS            0x3E    /* GNSS system configuration */

/* UBX-NAV Messages */
#define UBX_NAV_POSLLH          0x02    /* Geodetic position */
#define UBX_NAV_STATUS          0x03    /* Receiver status */
#define UBX_NAV_DOP             0x04    /* Dilution of precision */
#define UBX_NAV_SOL             0x06    /* Navigation solution */
#define UBX_NAV_PVT             0x07    /* Position, velocity, time */
#define UBX_NAV_VELNED          0x12    /* Velocity NED */
#define UBX_NAV_TIMEUTC         0x21    /* UTC time */
#define UBX_NAV_SAT             0x35    /* Satellite information */

/* UBX-ACK Messages */
#define UBX_ACK_NAK             0x00
#define UBX_ACK_ACK             0x01

/* Default baud rates */
#define NEO_M8N_DEFAULT_BAUD    9600
#define NEO_M8N_TARGET_BAUD     115200

/* Buffer sizes */
#define RX_BUFFER_SIZE          2048
#define TX_BUFFER_SIZE          256

/*
 * ============================================================
 * UBX Message Structure
 * ============================================================
 */

struct ubx_header {
    u8 sync1;
    u8 sync2;
    u8 msg_class;
    u8 msg_id;
    u16 length;
} __packed;

struct ubx_nav_pvt {
    u32 itow;           /* GPS time of week */
    u16 year;
    u8 month;
    u8 day;
    u8 hour;
    u8 min;
    u8 sec;
    u8 valid;
    u32 tacc;           /* Time accuracy estimate */
    s32 nano;           /* Nanosecond of second */
    u8 fix_type;
    u8 flags;
    u8 flags2;
    u8 num_sv;          /* Number of satellites */
    s32 lon;            /* Longitude (1e-7 degrees) */
    s32 lat;            /* Latitude (1e-7 degrees) */
    s32 height;         /* Height above ellipsoid (mm) */
    s32 hmsl;           /* Height above MSL (mm) */
    u32 hacc;           /* Horizontal accuracy (mm) */
    u32 vacc;           /* Vertical accuracy (mm) */
    s32 veln;           /* NED north velocity (mm/s) */
    s32 vele;           /* NED east velocity (mm/s) */
    s32 veld;           /* NED down velocity (mm/s) */
    s32 gspeed;         /* Ground speed (mm/s) */
    s32 headmot;        /* Heading of motion (1e-5 degrees) */
    u32 sacc;           /* Speed accuracy (mm/s) */
    u32 headacc;        /* Heading accuracy (1e-5 degrees) */
    u16 pdop;           /* Position DOP */
    u8 reserved1[6];
    s32 headveh;        /* Heading of vehicle (1e-5 degrees) */
    s16 magdec;         /* Magnetic declination (1e-2 degrees) */
    u16 magacc;         /* Magnetic declination accuracy */
} __packed;

/*
 * ============================================================
 * Device Structure
 * ============================================================
 */

struct ublox_neo_m8n {
    struct serdev_device *serdev;
    struct gnss_device *gdev;

    /* Power control */
    struct regulator *vcc;
    struct gpio_desc *reset_gpio;
    struct gpio_desc *extint_gpio;

    /* Communication state */
    speed_t current_baud;
    bool configured;

    /* Receive buffer */
    u8 rx_buffer[RX_BUFFER_SIZE];
    size_t rx_len;

    /* UBX message parsing state */
    enum {
        UBX_WAIT_SYNC1,
        UBX_WAIT_SYNC2,
        UBX_WAIT_CLASS,
        UBX_WAIT_ID,
        UBX_WAIT_LEN1,
        UBX_WAIT_LEN2,
        UBX_WAIT_PAYLOAD,
        UBX_WAIT_CK_A,
        UBX_WAIT_CK_B,
    } ubx_state;

    u8 ubx_class;
    u8 ubx_id;
    u16 ubx_len;
    u16 ubx_idx;
    u8 ubx_ck_a;
    u8 ubx_ck_b;
    u8 ubx_payload[512];

    /* ACK handling */
    struct completion ack_completion;
    bool ack_received;
    u8 ack_class;
    u8 ack_id;

    /* Statistics */
    unsigned long nmea_count;
    unsigned long ubx_count;
    unsigned long errors;
};

/*
 * ============================================================
 * UBX Checksum Calculation
 * ============================================================
 */

static void ubx_checksum(const u8 *data, size_t len, u8 *ck_a, u8 *ck_b)
{
    *ck_a = 0;
    *ck_b = 0;

    while (len--) {
        *ck_a += *data++;
        *ck_b += *ck_a;
    }
}

/*
 * ============================================================
 * UBX Message Construction
 * ============================================================
 */

static int ubx_build_message(u8 *buf, u8 msg_class, u8 msg_id,
                             const u8 *payload, u16 payload_len)
{
    u8 ck_a, ck_b;
    int idx = 0;

    buf[idx++] = UBX_SYNC1;
    buf[idx++] = UBX_SYNC2;
    buf[idx++] = msg_class;
    buf[idx++] = msg_id;
    buf[idx++] = payload_len & 0xFF;
    buf[idx++] = (payload_len >> 8) & 0xFF;

    if (payload && payload_len > 0) {
        memcpy(&buf[idx], payload, payload_len);
        idx += payload_len;
    }

    /* Calculate checksum over class, id, length, and payload */
    ubx_checksum(&buf[2], 4 + payload_len, &ck_a, &ck_b);
    buf[idx++] = ck_a;
    buf[idx++] = ck_b;

    return idx;
}

/*
 * ============================================================
 * UBX Message Sending
 * ============================================================
 */

static int ublox_send_ubx(struct ublox_neo_m8n *priv, u8 msg_class, u8 msg_id,
                          const u8 *payload, u16 payload_len)
{
    u8 buf[TX_BUFFER_SIZE];
    int len;

    len = ubx_build_message(buf, msg_class, msg_id, payload, payload_len);

    return serdev_device_write_buf(priv->serdev, buf, len);
}

static int ublox_send_ubx_wait_ack(struct ublox_neo_m8n *priv,
                                   u8 msg_class, u8 msg_id,
                                   const u8 *payload, u16 payload_len)
{
    int ret;

    reinit_completion(&priv->ack_completion);
    priv->ack_received = false;

    ret = ublox_send_ubx(priv, msg_class, msg_id, payload, payload_len);
    if (ret < 0)
        return ret;

    /* Wait for ACK/NAK */
    if (!wait_for_completion_timeout(&priv->ack_completion,
                                     msecs_to_jiffies(1000))) {
        dev_warn(&priv->serdev->dev, "UBX ACK timeout\n");
        return -ETIMEDOUT;
    }

    if (!priv->ack_received) {
        dev_warn(&priv->serdev->dev, "UBX NAK received\n");
        return -EPROTO;
    }

    return 0;
}

/*
 * ============================================================
 * UBX Message Parsing
 * ============================================================
 */

static void ublox_handle_ubx_message(struct ublox_neo_m8n *priv)
{
    priv->ubx_count++;

    switch (priv->ubx_class) {
    case UBX_CLASS_ACK:
        if (priv->ubx_id == UBX_ACK_ACK) {
            priv->ack_received = true;
            if (priv->ubx_len >= 2) {
                priv->ack_class = priv->ubx_payload[0];
                priv->ack_id = priv->ubx_payload[1];
            }
        } else if (priv->ubx_id == UBX_ACK_NAK) {
            priv->ack_received = false;
        }
        complete(&priv->ack_completion);
        break;

    case UBX_CLASS_NAV:
        /* Navigation messages can be forwarded to GNSS device */
        if (priv->ubx_id == UBX_NAV_PVT && priv->ubx_len >= sizeof(struct ubx_nav_pvt)) {
            /* Parse and log PVT data if needed */
        }
        break;

    default:
        break;
    }
}

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
            priv->ubx_state = UBX_WAIT_SYNC1;
        break;

    case UBX_WAIT_CLASS:
        priv->ubx_class = byte;
        priv->ubx_ck_a = byte;
        priv->ubx_ck_b = byte;
        priv->ubx_state = UBX_WAIT_ID;
        break;

    case UBX_WAIT_ID:
        priv->ubx_id = byte;
        priv->ubx_ck_a += byte;
        priv->ubx_ck_b += priv->ubx_ck_a;
        priv->ubx_state = UBX_WAIT_LEN1;
        break;

    case UBX_WAIT_LEN1:
        priv->ubx_len = byte;
        priv->ubx_ck_a += byte;
        priv->ubx_ck_b += priv->ubx_ck_a;
        priv->ubx_state = UBX_WAIT_LEN2;
        break;

    case UBX_WAIT_LEN2:
        priv->ubx_len |= (u16)byte << 8;
        priv->ubx_ck_a += byte;
        priv->ubx_ck_b += priv->ubx_ck_a;
        priv->ubx_idx = 0;

        if (priv->ubx_len > sizeof(priv->ubx_payload)) {
            priv->ubx_state = UBX_WAIT_SYNC1;
            priv->errors++;
        } else if (priv->ubx_len == 0) {
            priv->ubx_state = UBX_WAIT_CK_A;
        } else {
            priv->ubx_state = UBX_WAIT_PAYLOAD;
        }
        break;

    case UBX_WAIT_PAYLOAD:
        priv->ubx_payload[priv->ubx_idx++] = byte;
        priv->ubx_ck_a += byte;
        priv->ubx_ck_b += priv->ubx_ck_a;

        if (priv->ubx_idx >= priv->ubx_len)
            priv->ubx_state = UBX_WAIT_CK_A;
        break;

    case UBX_WAIT_CK_A:
        if (byte == priv->ubx_ck_a) {
            priv->ubx_state = UBX_WAIT_CK_B;
        } else {
            priv->ubx_state = UBX_WAIT_SYNC1;
            priv->errors++;
        }
        break;

    case UBX_WAIT_CK_B:
        if (byte == priv->ubx_ck_b) {
            ublox_handle_ubx_message(priv);
        } else {
            priv->errors++;
        }
        priv->ubx_state = UBX_WAIT_SYNC1;
        break;
    }
}

/*
 * ============================================================
 * Serial Receive Callback
 * ============================================================
 */

static size_t ublox_receive_buf(struct serdev_device *serdev,
                                const u8 *buf, size_t len)
{
    struct ublox_neo_m8n *priv = serdev_device_get_drvdata(serdev);
    size_t i;
    int ret;

    /* Forward raw data to GNSS device (for NMEA) */
    ret = gnss_insert_raw(priv->gdev, buf, len);
    if (ret < 0)
        dev_warn(&serdev->dev, "GNSS insert failed: %d\n", ret);

    /* Also parse for UBX messages */
    for (i = 0; i < len; i++) {
        if (buf[i] == UBX_SYNC1) {
            ublox_parse_ubx_byte(priv, buf[i]);
        } else if (priv->ubx_state != UBX_WAIT_SYNC1) {
            ublox_parse_ubx_byte(priv, buf[i]);
        }

        /* Count NMEA sentences */
        if (buf[i] == '$')
            priv->nmea_count++;
    }

    return len;
}

static const struct serdev_device_ops ublox_serdev_ops = {
    .receive_buf = ublox_receive_buf,
    .write_wakeup = serdev_device_write_wakeup,
};

/*
 * ============================================================
 * GNSS Device Operations
 * ============================================================
 */

static int ublox_gnss_open(struct gnss_device *gdev)
{
    struct ublox_neo_m8n *priv = gnss_get_drvdata(gdev);

    return serdev_device_open(priv->serdev);
}

static void ublox_gnss_close(struct gnss_device *gdev)
{
    struct ublox_neo_m8n *priv = gnss_get_drvdata(gdev);

    serdev_device_close(priv->serdev);
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

/*
 * ============================================================
 * Device Configuration
 * ============================================================
 */

static int ublox_configure_port(struct ublox_neo_m8n *priv, speed_t baud)
{
    u8 cfg_prt[20] = {0};

    /* Configure UART1 port
     * [0]    - Port ID (1 = UART1)
     * [1]    - Reserved
     * [2-3]  - TX ready
     * [4-7]  - Mode (8N1)
     * [8-11] - Baud rate
     * [12-13] - Input protocols (UBX + NMEA)
     * [14-15] - Output protocols (UBX + NMEA)
     * [16-17] - Flags
     * [18-19] - Reserved
     */
    cfg_prt[0] = 1;  /* UART1 */
    cfg_prt[4] = 0xC0;  /* 8-bit */
    cfg_prt[5] = 0x08;  /* No parity, 1 stop bit */

    /* Baud rate (little-endian) */
    cfg_prt[8] = baud & 0xFF;
    cfg_prt[9] = (baud >> 8) & 0xFF;
    cfg_prt[10] = (baud >> 16) & 0xFF;
    cfg_prt[11] = (baud >> 24) & 0xFF;

    /* Input: UBX + NMEA */
    cfg_prt[12] = 0x03;

    /* Output: UBX + NMEA */
    cfg_prt[14] = 0x03;

    return ublox_send_ubx_wait_ack(priv, UBX_CLASS_CFG, UBX_CFG_PRT,
                                   cfg_prt, sizeof(cfg_prt));
}

static int ublox_configure_rate(struct ublox_neo_m8n *priv, u16 rate_ms)
{
    u8 cfg_rate[6];

    /* Measurement rate (ms) */
    cfg_rate[0] = rate_ms & 0xFF;
    cfg_rate[1] = (rate_ms >> 8) & 0xFF;

    /* Navigation rate (cycles) - 1 = every measurement */
    cfg_rate[2] = 1;
    cfg_rate[3] = 0;

    /* Time reference - 1 = GPS time */
    cfg_rate[4] = 1;
    cfg_rate[5] = 0;

    return ublox_send_ubx_wait_ack(priv, UBX_CLASS_CFG, UBX_CFG_RATE,
                                   cfg_rate, sizeof(cfg_rate));
}

static int ublox_enable_message(struct ublox_neo_m8n *priv,
                                u8 msg_class, u8 msg_id, u8 rate)
{
    u8 cfg_msg[3];

    cfg_msg[0] = msg_class;
    cfg_msg[1] = msg_id;
    cfg_msg[2] = rate;  /* Rate for current port */

    return ublox_send_ubx_wait_ack(priv, UBX_CLASS_CFG, UBX_CFG_MSG,
                                   cfg_msg, sizeof(cfg_msg));
}

static int ublox_configure_device(struct ublox_neo_m8n *priv)
{
    int ret;

    /* Enable NAV-PVT message at 1 Hz */
    ret = ublox_enable_message(priv, UBX_CLASS_NAV, UBX_NAV_PVT, 1);
    if (ret && ret != -ETIMEDOUT) {
        dev_warn(&priv->serdev->dev, "Failed to enable NAV-PVT\n");
    }

    /* Set measurement rate to 1 Hz (1000 ms) */
    ret = ublox_configure_rate(priv, 1000);
    if (ret && ret != -ETIMEDOUT) {
        dev_warn(&priv->serdev->dev, "Failed to set rate\n");
    }

    priv->configured = true;
    return 0;
}

/*
 * ============================================================
 * Power Control
 * ============================================================
 */

static int ublox_power_on(struct ublox_neo_m8n *priv)
{
    int ret;

    /* Enable VCC */
    if (priv->vcc) {
        ret = regulator_enable(priv->vcc);
        if (ret) {
            dev_err(&priv->serdev->dev, "Failed to enable VCC\n");
            return ret;
        }
    }

    /* Deassert reset */
    if (priv->reset_gpio) {
        gpiod_set_value_cansleep(priv->reset_gpio, 0);
        msleep(100);  /* Wait for device to start */
    }

    return 0;
}

static void ublox_power_off(struct ublox_neo_m8n *priv)
{
    /* Assert reset */
    if (priv->reset_gpio)
        gpiod_set_value_cansleep(priv->reset_gpio, 1);

    /* Disable VCC */
    if (priv->vcc)
        regulator_disable(priv->vcc);
}

/*
 * ============================================================
 * Probe and Remove
 * ============================================================
 */

static int ublox_neo_m8n_probe(struct serdev_device *serdev)
{
    struct device *dev = &serdev->dev;
    struct ublox_neo_m8n *priv;
    int ret;

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->serdev = serdev;
    init_completion(&priv->ack_completion);
    priv->ubx_state = UBX_WAIT_SYNC1;

    serdev_device_set_drvdata(serdev, priv);

    /* Get optional VCC regulator */
    priv->vcc = devm_regulator_get_optional(dev, "vcc");
    if (IS_ERR(priv->vcc)) {
        ret = PTR_ERR(priv->vcc);
        if (ret == -ENODEV)
            priv->vcc = NULL;
        else
            return ret;
    }

    /* Get optional reset GPIO */
    priv->reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
    if (IS_ERR(priv->reset_gpio))
        return PTR_ERR(priv->reset_gpio);

    /* Get optional EXTINT GPIO (for wakeup) */
    priv->extint_gpio = devm_gpiod_get_optional(dev, "extint", GPIOD_OUT_LOW);
    if (IS_ERR(priv->extint_gpio))
        return PTR_ERR(priv->extint_gpio);

    /* Allocate GNSS device */
    priv->gdev = gnss_allocate_device(dev);
    if (!priv->gdev)
        return -ENOMEM;

    priv->gdev->type = GNSS_TYPE_UBX;
    priv->gdev->ops = &ublox_gnss_ops;
    gnss_set_drvdata(priv->gdev, priv);

    /* Power on device */
    ret = ublox_power_on(priv);
    if (ret)
        goto err_free_gnss;

    /* Open serial port */
    ret = serdev_device_open(serdev);
    if (ret)
        goto err_power_off;

    /* Set up serial port */
    serdev_device_set_client_ops(serdev, &ublox_serdev_ops);
    serdev_device_set_baudrate(serdev, NEO_M8N_DEFAULT_BAUD);
    serdev_device_set_flow_control(serdev, false);
    priv->current_baud = NEO_M8N_DEFAULT_BAUD;

    /* Register GNSS device */
    ret = gnss_register_device(priv->gdev);
    if (ret)
        goto err_close;

    /* Configure device */
    ublox_configure_device(priv);

    dev_info(dev, "u-blox NEO-M8N GNSS registered\n");
    return 0;

err_close:
    serdev_device_close(serdev);
err_power_off:
    ublox_power_off(priv);
err_free_gnss:
    gnss_put_device(priv->gdev);
    return ret;
}

static void ublox_neo_m8n_remove(struct serdev_device *serdev)
{
    struct ublox_neo_m8n *priv = serdev_device_get_drvdata(serdev);

    gnss_deregister_device(priv->gdev);
    serdev_device_close(serdev);
    ublox_power_off(priv);
    gnss_put_device(priv->gdev);

    dev_info(&serdev->dev, "Stats: NMEA=%lu, UBX=%lu, Errors=%lu\n",
             priv->nmea_count, priv->ubx_count, priv->errors);
}

/*
 * ============================================================
 * Power Management
 * ============================================================
 */

#ifdef CONFIG_PM_SLEEP
static int ublox_suspend(struct device *dev)
{
    struct ublox_neo_m8n *priv = dev_get_drvdata(dev);

    /* Send UBX-RXM-PMREQ to put device in backup mode */
    /* Or just power off */
    ublox_power_off(priv);

    return 0;
}

static int ublox_resume(struct device *dev)
{
    struct ublox_neo_m8n *priv = dev_get_drvdata(dev);
    int ret;

    ret = ublox_power_on(priv);
    if (ret)
        return ret;

    /* Reconfigure if needed */
    if (priv->configured)
        ublox_configure_device(priv);

    return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ublox_pm_ops, ublox_suspend, ublox_resume);

/*
 * ============================================================
 * Driver Registration
 * ============================================================
 */

static const struct of_device_id ublox_neo_m8n_of_match[] = {
    { .compatible = "u-blox,neo-m8" },
    { .compatible = "u-blox,neo-m8n" },
    { .compatible = "u-blox,neo-m8m" },
    { .compatible = "u-blox,neo-m8q" },
    { .compatible = "u-blox,neo-m9n" },
    { }
};
MODULE_DEVICE_TABLE(of, ublox_neo_m8n_of_match);

static struct serdev_device_driver ublox_neo_m8n_driver = {
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = ublox_neo_m8n_of_match,
        .pm = &ublox_pm_ops,
    },
    .probe = ublox_neo_m8n_probe,
    .remove = ublox_neo_m8n_remove,
};
module_serdev_device_driver(ublox_neo_m8n_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("u-blox NEO-M8N GPS/GNSS Driver");
MODULE_VERSION("1.0");
