# Custom SPI Controller Driver Tutorial

Guide to implementing an SPI master controller driver with DMA support.

## What This Driver Demonstrates

| Feature | Implementation | Purpose |
|---------|---------------|---------|
| SPI Controller | `spi_controller` | Master controller driver |
| Transfer Engine | `transfer_one` | Single transfer handling |
| DMA Support | Scatter-gather DMA | High-speed transfers |
| GPIO Chip Select | `gpiod_*` API | Flexible CS control |
| Clock Management | `clk` API | Dynamic clock control |

---

## SPI Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                          USER SPACE                                  │
│   Application using spidev (/dev/spidev0.0)                         │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        SPI CORE                                      │
│                                                                      │
│   spi_device (slave) ←──→ spi_controller (master)                   │
│       │                         │                                    │
│       │   spi_message           │                                    │
│       │   └─ spi_transfer       │                                    │
│       │      └─ spi_transfer    │                                    │
│       │                         │                                    │
│       └─────────────────────────┘                                    │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                   SPI CONTROLLER DRIVER                              │
│                                                                      │
│   spi_controller_ops:                                                │
│   ├─ .setup()          ──→ Per-device configuration                 │
│   ├─ .transfer_one()   ──→ Execute single transfer                  │
│   ├─ .set_cs()         ──→ Control chip select                      │
│   └─ .cleanup()        ──→ Per-device cleanup                       │
│                                                                      │
│   Transfer Flow:                                                     │
│   1. set_cs(true)      ──→ Assert chip select                       │
│   2. For each transfer:                                              │
│      transfer_one()    ──→ Clock data in/out                        │
│   3. set_cs(false)     ──→ Deassert chip select                     │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                       SPI HARDWARE                                   │
│                                                                      │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │   CR0: Data size, clock polarity/phase, speed               │   │
│   │   CR1: Enable, master/slave mode                            │   │
│   │   DR:  Data register (TX/RX FIFO)                           │   │
│   │   SR:  Status (FIFO status, busy)                           │   │
│   └─────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Key Code Patterns

### 1. Controller Registration

```c
static int custom_spi_probe(struct platform_device *pdev)
{
    struct spi_controller *ctlr;

    /* Allocate controller + private data */
    ctlr = devm_spi_alloc_master(&pdev->dev, sizeof(*spi));

    /* Configure controller */
    ctlr->bus_num = -1;           /* Auto-assign */
    ctlr->num_chipselect = 4;
    ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH;
    ctlr->bits_per_word_mask = SPI_BPW_MASK(8) | SPI_BPW_MASK(16);
    ctlr->max_speed_hz = 50000000;

    /* Set operations */
    ctlr->setup = custom_spi_setup;
    ctlr->transfer_one = custom_spi_transfer_one;
    ctlr->set_cs = custom_spi_set_cs;

    /* Register */
    return devm_spi_register_controller(&pdev->dev, ctlr);
}
```

### 2. Transfer Function

```c
static int custom_spi_transfer_one(struct spi_controller *ctlr,
                                   struct spi_device *spi,
                                   struct spi_transfer *xfer)
{
    struct custom_spi *priv = spi_controller_get_devdata(ctlr);

    /* Configure speed and mode */
    custom_spi_config(priv, spi, xfer);

    /* Choose DMA or PIO based on transfer size */
#ifdef CONFIG_SPI_CUSTOM_DMA
    if (xfer->len >= DMA_MIN_BYTES)
        return custom_spi_transfer_dma(priv, xfer);
#endif

    return custom_spi_transfer_pio(priv, xfer);
}
```

### 3. PIO Transfer

```c
static int custom_spi_transfer_pio(struct custom_spi *spi,
                                   struct spi_transfer *xfer)
{
    const u8 *tx = xfer->tx_buf;
    u8 *rx = xfer->rx_buf;
    size_t len = xfer->len;

    while (len--) {
        /* Wait for TX FIFO not full */
        while (!(spi_read(spi, SPI_SR) & SPI_SR_TNF))
            cpu_relax();

        /* Write TX data (or dummy if RX-only) */
        spi_write(spi, SPI_DR, tx ? *tx++ : 0);

        /* Wait for RX FIFO not empty */
        while (!(spi_read(spi, SPI_SR) & SPI_SR_RNE))
            cpu_relax();

        /* Read RX data */
        u8 data = spi_read(spi, SPI_DR);
        if (rx)
            *rx++ = data;
    }

    return 0;
}
```

### 4. Chip Select Control

```c
static void custom_spi_set_cs(struct spi_device *spi, bool enable)
{
    struct custom_spi *priv = spi_controller_get_devdata(spi->controller);

    if (priv->cs_gpios[spi->chip_select]) {
        /* GPIO-based CS */
        gpiod_set_value(priv->cs_gpios[spi->chip_select], enable);
    } else {
        /* Hardware CS control */
        /* ... */
    }
}
```

### 5. IRQ-based Transfer

```c
static irqreturn_t custom_spi_irq(int irq, void *dev_id)
{
    struct custom_spi *spi = dev_id;
    u32 status = spi_read(spi, SPI_MIS);

    if (status & SPI_INT_RX) {
        /* RX FIFO threshold reached */
        while (spi_read(spi, SPI_SR) & SPI_SR_RNE) {
            u8 data = spi_read(spi, SPI_DR);
            if (spi->rx_buf && spi->rx_len > 0) {
                *spi->rx_buf++ = data;
                spi->rx_len--;
            }
        }
    }

    if (status & SPI_INT_TX) {
        /* TX FIFO needs more data */
        while ((spi_read(spi, SPI_SR) & SPI_SR_TNF) && spi->tx_len > 0) {
            spi_write(spi, SPI_DR, spi->tx_buf ? *spi->tx_buf++ : 0);
            spi->tx_len--;
        }
    }

    if (spi->rx_len == 0 && spi->tx_len == 0)
        complete(&spi->done);

    return IRQ_HANDLED;
}
```

---

## Device Tree

```dts
spi0: spi@20000000 {
    compatible = "vendor,custom-spi";
    reg = <0x20000000 0x100>;
    interrupts = <GIC_SPI 41 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&spi_clk>;
    clock-names = "spi";

    #address-cells = <1>;
    #size-cells = <0>;

    /* GPIO chip selects */
    cs-gpios = <&gpio 8 GPIO_ACTIVE_LOW>,
               <&gpio 7 GPIO_ACTIVE_LOW>;

    /* Optional DMA */
    dmas = <&dma 0>, <&dma 1>;
    dma-names = "rx", "tx";

    /* SPI device */
    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <50000000>;
    };
};
```

---

## Key Learning Points

1. **Controller vs Client** - Controller driver for master hardware, client driver for slave devices
2. **Transfer API** - `transfer_one` for modern async API, `transfer_one_message` for batched
3. **DMA Threshold** - Use DMA for large transfers, PIO for small (DMA setup overhead)
4. **CS Timing** - Some devices need delays after CS assert/deassert
5. **Mode Bits** - CPOL/CPHA determine clock polarity/phase
