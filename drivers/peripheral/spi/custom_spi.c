/*
 * custom_spi.c - Full-Featured SPI Controller Driver
 *
 * This is a complete SPI master controller driver demonstrating:
 * - SPI core framework integration
 * - Interrupt-driven transfers
 * - DMA support with scatter-gather
 * - Multiple chip select support
 * - Power management
 * - Device tree configuration
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/pm_runtime.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>

#define DRIVER_NAME     "custom_spi"
#define MAX_CS          4       /* Maximum chip selects */
#define FIFO_DEPTH      64      /* Hardware FIFO depth */

/*
 * ============================================================
 * Register Definitions
 * ============================================================
 */

/* Register offsets */
#define SPI_CR0         0x00    /* Control Register 0 */
#define SPI_CR1         0x04    /* Control Register 1 */
#define SPI_DR          0x08    /* Data Register */
#define SPI_SR          0x0C    /* Status Register */
#define SPI_CPSR        0x10    /* Clock Prescale Register */
#define SPI_IMSC        0x14    /* Interrupt Mask Set/Clear */
#define SPI_RIS         0x18    /* Raw Interrupt Status */
#define SPI_MIS         0x1C    /* Masked Interrupt Status */
#define SPI_ICR         0x20    /* Interrupt Clear Register */
#define SPI_DMACR       0x24    /* DMA Control Register */

/* CR0 bits */
#define SPI_CR0_DSS_MASK    0x000F  /* Data Size Select (bits-1) */
#define SPI_CR0_FRF_MASK    0x0030  /* Frame Format */
#define SPI_CR0_FRF_SPI     0x0000  /* Motorola SPI */
#define SPI_CR0_FRF_TI      0x0010  /* TI SSP */
#define SPI_CR0_FRF_MW      0x0020  /* Microwire */
#define SPI_CR0_SPO         BIT(6)  /* Clock polarity */
#define SPI_CR0_SPH         BIT(7)  /* Clock phase */
#define SPI_CR0_SCR_MASK    0xFF00  /* Serial clock rate */
#define SPI_CR0_SCR_SHIFT   8

/* CR1 bits */
#define SPI_CR1_LBM         BIT(0)  /* Loopback mode */
#define SPI_CR1_SSE         BIT(1)  /* SPI enable */
#define SPI_CR1_MS          BIT(2)  /* Master/Slave select (0=master) */
#define SPI_CR1_SOD         BIT(3)  /* Slave output disable */

/* SR bits */
#define SPI_SR_TFE          BIT(0)  /* TX FIFO empty */
#define SPI_SR_TNF          BIT(1)  /* TX FIFO not full */
#define SPI_SR_RNE          BIT(2)  /* RX FIFO not empty */
#define SPI_SR_RFF          BIT(3)  /* RX FIFO full */
#define SPI_SR_BSY          BIT(4)  /* SPI busy */

/* Interrupt bits (IMSC, RIS, MIS, ICR) */
#define SPI_INT_ROR         BIT(0)  /* RX overrun */
#define SPI_INT_RT          BIT(1)  /* RX timeout */
#define SPI_INT_RX          BIT(2)  /* RX FIFO half full */
#define SPI_INT_TX          BIT(3)  /* TX FIFO half empty */
#define SPI_INT_ALL         0x0F

/* DMACR bits */
#define SPI_DMACR_RXDMAE    BIT(0)  /* RX DMA enable */
#define SPI_DMACR_TXDMAE    BIT(1)  /* TX DMA enable */

/*
 * ============================================================
 * Driver Data Structures
 * ============================================================
 */

struct custom_spi_dma {
    struct dma_chan         *chan;
    struct dma_slave_config cfg;
    struct sg_table         sgt;
    enum dma_data_direction dir;
};

struct custom_spi {
    struct spi_controller   *controller;
    struct device           *dev;
    void __iomem            *regs;
    struct clk              *clk;
    int                     irq;

    /* Transfer state */
    const u8                *tx_buf;
    u8                      *rx_buf;
    size_t                  tx_len;
    size_t                  rx_len;
    size_t                  count;
    unsigned int            bits_per_word;

    /* Completion for synchronous transfers */
    struct completion       done;
    int                     status;

    /* DMA */
    bool                    dma_enabled;
    struct custom_spi_dma   tx_dma;
    struct custom_spi_dma   rx_dma;
    dma_addr_t              phys_addr;

    /* GPIO chip selects */
    struct gpio_desc        *cs_gpios[MAX_CS];
    int                     num_cs;

    /* Statistics */
    unsigned long           transfers;
    unsigned long           bytes_tx;
    unsigned long           bytes_rx;
    unsigned long           errors;
};

/*
 * ============================================================
 * Register Access
 * ============================================================
 */

static inline u32 spi_read(struct custom_spi *spi, int reg)
{
    return readl(spi->regs + reg);
}

static inline void spi_write(struct custom_spi *spi, int reg, u32 val)
{
    writel(val, spi->regs + reg);
}

static inline void spi_set_bits(struct custom_spi *spi, int reg, u32 bits)
{
    u32 val = spi_read(spi, reg);
    spi_write(spi, reg, val | bits);
}

static inline void spi_clear_bits(struct custom_spi *spi, int reg, u32 bits)
{
    u32 val = spi_read(spi, reg);
    spi_write(spi, reg, val & ~bits);
}

/*
 * ============================================================
 * Hardware Control
 * ============================================================
 */

static void custom_spi_enable(struct custom_spi *spi)
{
    spi_set_bits(spi, SPI_CR1, SPI_CR1_SSE);
}

static void custom_spi_disable(struct custom_spi *spi)
{
    spi_clear_bits(spi, SPI_CR1, SPI_CR1_SSE);
}

static void custom_spi_flush_fifo(struct custom_spi *spi)
{
    unsigned int timeout = 1000;

    /* Read until FIFO empty */
    while ((spi_read(spi, SPI_SR) & SPI_SR_RNE) && timeout--)
        spi_read(spi, SPI_DR);

    if (!timeout)
        dev_warn(spi->dev, "FIFO flush timeout\n");
}

static int custom_spi_wait_idle(struct custom_spi *spi)
{
    unsigned int timeout = 10000;

    while ((spi_read(spi, SPI_SR) & SPI_SR_BSY) && timeout--)
        cpu_relax();

    if (!timeout) {
        dev_err(spi->dev, "SPI busy timeout\n");
        return -ETIMEDOUT;
    }

    return 0;
}

/*
 * ============================================================
 * Clock Configuration
 * ============================================================
 */

static int custom_spi_set_speed(struct custom_spi *spi, u32 speed_hz)
{
    u32 spi_clk, cpsr, scr;
    u32 cr0;

    spi_clk = clk_get_rate(spi->clk);
    if (!spi_clk) {
        dev_err(spi->dev, "SPI clock rate is 0\n");
        return -EINVAL;
    }

    /*
     * SPI clock = spi_clk / (CPSR * (1 + SCR))
     * CPSR: 2-254 (even numbers only)
     * SCR: 0-255
     */

    /* Find prescaler values */
    for (cpsr = 2; cpsr <= 254; cpsr += 2) {
        scr = DIV_ROUND_UP(spi_clk, cpsr * speed_hz) - 1;
        if (scr <= 255)
            break;
    }

    if (cpsr > 254) {
        dev_err(spi->dev, "Cannot achieve %u Hz (min %lu Hz)\n",
                speed_hz, spi_clk / (254 * 256));
        return -EINVAL;
    }

    /* Update SCR in CR0 */
    cr0 = spi_read(spi, SPI_CR0);
    cr0 &= ~SPI_CR0_SCR_MASK;
    cr0 |= (scr << SPI_CR0_SCR_SHIFT);
    spi_write(spi, SPI_CR0, cr0);

    /* Set prescaler */
    spi_write(spi, SPI_CPSR, cpsr);

    return 0;
}

/*
 * ============================================================
 * Mode and Format Configuration
 * ============================================================
 */

static void custom_spi_set_mode(struct custom_spi *spi, u32 mode)
{
    u32 cr0 = spi_read(spi, SPI_CR0);

    cr0 &= ~(SPI_CR0_SPO | SPI_CR0_SPH);

    if (mode & SPI_CPOL)
        cr0 |= SPI_CR0_SPO;
    if (mode & SPI_CPHA)
        cr0 |= SPI_CR0_SPH;

    spi_write(spi, SPI_CR0, cr0);
}

static void custom_spi_set_bits_per_word(struct custom_spi *spi, u8 bits)
{
    u32 cr0 = spi_read(spi, SPI_CR0);

    cr0 &= ~SPI_CR0_DSS_MASK;
    cr0 |= (bits - 1);

    spi->bits_per_word = bits;
    spi_write(spi, SPI_CR0, cr0);
}

/*
 * ============================================================
 * Chip Select Control
 * ============================================================
 */

static void custom_spi_cs_control(struct custom_spi *spi,
                                  struct spi_device *spi_dev,
                                  bool enable)
{
    int cs = spi_get_chipselect(spi_dev, 0);

    if (cs >= spi->num_cs || !spi->cs_gpios[cs])
        return;

    if (spi_dev->mode & SPI_CS_HIGH)
        gpiod_set_value_cansleep(spi->cs_gpios[cs], enable ? 1 : 0);
    else
        gpiod_set_value_cansleep(spi->cs_gpios[cs], enable ? 0 : 1);
}

/*
 * ============================================================
 * PIO (Programmed I/O) Transfer
 * ============================================================
 */

static void custom_spi_pio_write(struct custom_spi *spi)
{
    u32 val;

    while (spi->tx_len &&
           (spi_read(spi, SPI_SR) & SPI_SR_TNF)) {

        if (spi->tx_buf) {
            if (spi->bits_per_word <= 8) {
                val = *spi->tx_buf++;
            } else {
                val = *(u16 *)spi->tx_buf;
                spi->tx_buf += 2;
            }
        } else {
            val = 0;  /* TX dummy data for read-only transfers */
        }

        spi_write(spi, SPI_DR, val);
        spi->tx_len--;
        spi->count++;
    }
}

static void custom_spi_pio_read(struct custom_spi *spi)
{
    u32 val;

    while (spi->rx_len &&
           (spi_read(spi, SPI_SR) & SPI_SR_RNE)) {

        val = spi_read(spi, SPI_DR);

        if (spi->rx_buf) {
            if (spi->bits_per_word <= 8) {
                *spi->rx_buf++ = val;
            } else {
                *(u16 *)spi->rx_buf = val;
                spi->rx_buf += 2;
            }
        }

        spi->rx_len--;
    }
}

static int custom_spi_pio_transfer(struct custom_spi *spi,
                                   struct spi_transfer *xfer)
{
    unsigned long timeout;

    spi->tx_buf = xfer->tx_buf;
    spi->rx_buf = xfer->rx_buf;
    spi->tx_len = xfer->len;
    spi->rx_len = xfer->len;
    spi->count = 0;

    reinit_completion(&spi->done);

    /* Enable interrupts */
    spi_write(spi, SPI_IMSC, SPI_INT_TX | SPI_INT_RX | SPI_INT_ROR);

    /* Fill TX FIFO */
    custom_spi_pio_write(spi);

    /* Wait for completion */
    timeout = wait_for_completion_timeout(&spi->done,
                                          msecs_to_jiffies(5000));
    if (!timeout) {
        dev_err(spi->dev, "PIO transfer timeout\n");
        spi->status = -ETIMEDOUT;
    }

    /* Disable interrupts */
    spi_write(spi, SPI_IMSC, 0);

    return spi->status;
}

/*
 * ============================================================
 * DMA Transfer
 * ============================================================
 */

static void custom_spi_dma_tx_callback(void *data)
{
    struct custom_spi *spi = data;

    /* TX DMA complete - nothing to do, wait for RX */
    spi->bytes_tx += spi->count;
}

static void custom_spi_dma_rx_callback(void *data)
{
    struct custom_spi *spi = data;

    /* RX DMA complete - transfer done */
    spi->bytes_rx += spi->count;
    spi->status = 0;
    complete(&spi->done);
}

static int custom_spi_dma_transfer(struct custom_spi *spi,
                                   struct spi_transfer *xfer)
{
    struct dma_async_tx_descriptor *tx_desc = NULL;
    struct dma_async_tx_descriptor *rx_desc = NULL;
    dma_cookie_t tx_cookie, rx_cookie;
    unsigned long timeout;
    int ret;

    spi->count = xfer->len;
    reinit_completion(&spi->done);

    /* Configure TX DMA */
    if (xfer->tx_buf) {
        tx_desc = dmaengine_prep_slave_single(spi->tx_dma.chan,
                                              xfer->tx_dma,
                                              xfer->len,
                                              DMA_MEM_TO_DEV,
                                              DMA_PREP_INTERRUPT);
        if (!tx_desc) {
            dev_err(spi->dev, "TX DMA prep failed\n");
            return -ENOMEM;
        }

        tx_desc->callback = custom_spi_dma_tx_callback;
        tx_desc->callback_param = spi;
    }

    /* Configure RX DMA */
    if (xfer->rx_buf) {
        rx_desc = dmaengine_prep_slave_single(spi->rx_dma.chan,
                                              xfer->rx_dma,
                                              xfer->len,
                                              DMA_DEV_TO_MEM,
                                              DMA_PREP_INTERRUPT);
        if (!rx_desc) {
            dev_err(spi->dev, "RX DMA prep failed\n");
            if (tx_desc)
                dmaengine_terminate_sync(spi->tx_dma.chan);
            return -ENOMEM;
        }

        rx_desc->callback = custom_spi_dma_rx_callback;
        rx_desc->callback_param = spi;
    }

    /* Enable DMA in SPI controller */
    spi_write(spi, SPI_DMACR, SPI_DMACR_TXDMAE | SPI_DMACR_RXDMAE);

    /* Submit DMA descriptors */
    if (rx_desc) {
        rx_cookie = dmaengine_submit(rx_desc);
        ret = dma_submit_error(rx_cookie);
        if (ret) {
            dev_err(spi->dev, "RX DMA submit failed\n");
            goto err_disable_dma;
        }
        dma_async_issue_pending(spi->rx_dma.chan);
    }

    if (tx_desc) {
        tx_cookie = dmaengine_submit(tx_desc);
        ret = dma_submit_error(tx_cookie);
        if (ret) {
            dev_err(spi->dev, "TX DMA submit failed\n");
            goto err_disable_dma;
        }
        dma_async_issue_pending(spi->tx_dma.chan);
    }

    /* Wait for completion */
    timeout = wait_for_completion_timeout(&spi->done,
                                          msecs_to_jiffies(5000));
    if (!timeout) {
        dev_err(spi->dev, "DMA transfer timeout\n");
        ret = -ETIMEDOUT;
        goto err_disable_dma;
    }

    ret = spi->status;

err_disable_dma:
    /* Disable DMA */
    spi_write(spi, SPI_DMACR, 0);

    if (ret) {
        dmaengine_terminate_sync(spi->tx_dma.chan);
        dmaengine_terminate_sync(spi->rx_dma.chan);
    }

    return ret;
}

static bool custom_spi_can_dma(struct spi_controller *ctlr,
                               struct spi_device *spi_dev,
                               struct spi_transfer *xfer)
{
    struct custom_spi *spi = spi_controller_get_devdata(ctlr);

    /* Use DMA for transfers larger than FIFO */
    if (!spi->dma_enabled)
        return false;

    return xfer->len > FIFO_DEPTH;
}

/*
 * ============================================================
 * Interrupt Handler
 * ============================================================
 */

static irqreturn_t custom_spi_irq(int irq, void *dev_id)
{
    struct custom_spi *spi = dev_id;
    u32 status;

    status = spi_read(spi, SPI_MIS);
    if (!status)
        return IRQ_NONE;

    /* Clear interrupts */
    spi_write(spi, SPI_ICR, status);

    /* RX overrun error */
    if (status & SPI_INT_ROR) {
        dev_err(spi->dev, "RX overrun error\n");
        spi->errors++;
        spi->status = -EIO;
        complete(&spi->done);
        return IRQ_HANDLED;
    }

    /* RX data available */
    if (status & (SPI_INT_RX | SPI_INT_RT)) {
        custom_spi_pio_read(spi);
    }

    /* TX FIFO needs data */
    if (status & SPI_INT_TX) {
        custom_spi_pio_write(spi);
    }

    /* Check if transfer complete */
    if (spi->tx_len == 0 && spi->rx_len == 0) {
        /* Disable interrupts */
        spi_write(spi, SPI_IMSC, 0);
        spi->status = 0;
        complete(&spi->done);
    }

    return IRQ_HANDLED;
}

/*
 * ============================================================
 * SPI Controller Operations
 * ============================================================
 */

static int custom_spi_prepare_message(struct spi_controller *ctlr,
                                      struct spi_message *msg)
{
    struct custom_spi *spi = spi_controller_get_devdata(ctlr);
    struct spi_device *spi_dev = msg->spi;

    /* Set mode for this device */
    custom_spi_set_mode(spi, spi_dev->mode);

    /* Assert chip select */
    custom_spi_cs_control(spi, spi_dev, true);

    return 0;
}

static int custom_spi_transfer_one(struct spi_controller *ctlr,
                                   struct spi_device *spi_dev,
                                   struct spi_transfer *xfer)
{
    struct custom_spi *spi = spi_controller_get_devdata(ctlr);
    int ret;

    /* Configure for this transfer */
    ret = custom_spi_set_speed(spi, xfer->speed_hz);
    if (ret)
        return ret;

    custom_spi_set_bits_per_word(spi, xfer->bits_per_word);

    /* Flush FIFOs */
    custom_spi_flush_fifo(spi);

    /* Enable SPI */
    custom_spi_enable(spi);

    spi->status = 0;

    /* Perform transfer */
    if (custom_spi_can_dma(ctlr, spi_dev, xfer) &&
        xfer->tx_dma && xfer->rx_dma) {
        ret = custom_spi_dma_transfer(spi, xfer);
    } else {
        ret = custom_spi_pio_transfer(spi, xfer);
    }

    /* Wait for transfer to complete */
    custom_spi_wait_idle(spi);

    /* Disable SPI */
    custom_spi_disable(spi);

    spi->transfers++;

    return ret;
}

static int custom_spi_unprepare_message(struct spi_controller *ctlr,
                                        struct spi_message *msg)
{
    struct custom_spi *spi = spi_controller_get_devdata(ctlr);
    struct spi_device *spi_dev = msg->spi;

    /* Deassert chip select */
    custom_spi_cs_control(spi, spi_dev, false);

    return 0;
}

static void custom_spi_set_cs(struct spi_device *spi_dev, bool enable)
{
    struct custom_spi *spi = spi_controller_get_devdata(spi_dev->controller);

    custom_spi_cs_control(spi, spi_dev, enable);
}

/*
 * ============================================================
 * DMA Initialization
 * ============================================================
 */

static int custom_spi_dma_init(struct custom_spi *spi)
{
    int ret;

    /* Request TX DMA channel */
    spi->tx_dma.chan = dma_request_chan(spi->dev, "tx");
    if (IS_ERR(spi->tx_dma.chan)) {
        ret = PTR_ERR(spi->tx_dma.chan);
        spi->tx_dma.chan = NULL;
        if (ret == -EPROBE_DEFER)
            return ret;
        dev_info(spi->dev, "TX DMA not available, using PIO\n");
        goto no_dma;
    }

    /* Request RX DMA channel */
    spi->rx_dma.chan = dma_request_chan(spi->dev, "rx");
    if (IS_ERR(spi->rx_dma.chan)) {
        ret = PTR_ERR(spi->rx_dma.chan);
        spi->rx_dma.chan = NULL;
        dma_release_channel(spi->tx_dma.chan);
        spi->tx_dma.chan = NULL;
        if (ret == -EPROBE_DEFER)
            return ret;
        dev_info(spi->dev, "RX DMA not available, using PIO\n");
        goto no_dma;
    }

    /* Configure TX DMA */
    spi->tx_dma.cfg.direction = DMA_MEM_TO_DEV;
    spi->tx_dma.cfg.dst_addr = spi->phys_addr + SPI_DR;
    spi->tx_dma.cfg.dst_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
    spi->tx_dma.cfg.dst_maxburst = 4;

    ret = dmaengine_slave_config(spi->tx_dma.chan, &spi->tx_dma.cfg);
    if (ret) {
        dev_err(spi->dev, "TX DMA config failed\n");
        goto err_release;
    }

    /* Configure RX DMA */
    spi->rx_dma.cfg.direction = DMA_DEV_TO_MEM;
    spi->rx_dma.cfg.src_addr = spi->phys_addr + SPI_DR;
    spi->rx_dma.cfg.src_addr_width = DMA_SLAVE_BUSWIDTH_1_BYTE;
    spi->rx_dma.cfg.src_maxburst = 4;

    ret = dmaengine_slave_config(spi->rx_dma.chan, &spi->rx_dma.cfg);
    if (ret) {
        dev_err(spi->dev, "RX DMA config failed\n");
        goto err_release;
    }

    spi->dma_enabled = true;
    dev_info(spi->dev, "DMA enabled\n");
    return 0;

err_release:
    dma_release_channel(spi->tx_dma.chan);
    dma_release_channel(spi->rx_dma.chan);
    spi->tx_dma.chan = NULL;
    spi->rx_dma.chan = NULL;
no_dma:
    spi->dma_enabled = false;
    return 0;
}

static void custom_spi_dma_release(struct custom_spi *spi)
{
    if (spi->tx_dma.chan) {
        dmaengine_terminate_sync(spi->tx_dma.chan);
        dma_release_channel(spi->tx_dma.chan);
    }

    if (spi->rx_dma.chan) {
        dmaengine_terminate_sync(spi->rx_dma.chan);
        dma_release_channel(spi->rx_dma.chan);
    }
}

/*
 * ============================================================
 * Hardware Initialization
 * ============================================================
 */

static int custom_spi_hw_init(struct custom_spi *spi)
{
    /* Disable SPI */
    custom_spi_disable(spi);

    /* Configure as master, SPI frame format */
    spi_write(spi, SPI_CR0, SPI_CR0_FRF_SPI | 0x07);  /* 8-bit */
    spi_write(spi, SPI_CR1, 0);  /* Master mode */

    /* Set default clock dividers */
    spi_write(spi, SPI_CPSR, 2);

    /* Clear interrupts */
    spi_write(spi, SPI_ICR, SPI_INT_ALL);

    /* Disable DMA */
    spi_write(spi, SPI_DMACR, 0);

    /* Flush FIFOs */
    custom_spi_flush_fifo(spi);

    return 0;
}

/*
 * ============================================================
 * Platform Driver
 * ============================================================
 */

static int custom_spi_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct spi_controller *ctlr;
    struct custom_spi *spi;
    struct resource *res;
    int ret, i;

    /* Allocate SPI controller */
    ctlr = devm_spi_alloc_host(dev, sizeof(*spi));
    if (!ctlr)
        return -ENOMEM;

    spi = spi_controller_get_devdata(ctlr);
    spi->dev = dev;
    spi->controller = ctlr;

    init_completion(&spi->done);

    /* Get memory resource */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    spi->regs = devm_ioremap_resource(dev, res);
    if (IS_ERR(spi->regs))
        return PTR_ERR(spi->regs);

    spi->phys_addr = res->start;

    /* Get clock */
    spi->clk = devm_clk_get(dev, NULL);
    if (IS_ERR(spi->clk)) {
        dev_err(dev, "Failed to get clock\n");
        return PTR_ERR(spi->clk);
    }

    /* Get IRQ */
    spi->irq = platform_get_irq(pdev, 0);
    if (spi->irq < 0)
        return spi->irq;

    /* Get GPIO chip selects */
    spi->num_cs = of_gpio_named_count(dev->of_node, "cs-gpios");
    if (spi->num_cs < 0)
        spi->num_cs = 1;
    if (spi->num_cs > MAX_CS)
        spi->num_cs = MAX_CS;

    for (i = 0; i < spi->num_cs; i++) {
        spi->cs_gpios[i] = devm_gpiod_get_index_optional(dev, "cs",
                                                          i, GPIOD_OUT_HIGH);
        if (IS_ERR(spi->cs_gpios[i])) {
            ret = PTR_ERR(spi->cs_gpios[i]);
            dev_err(dev, "Failed to get CS GPIO %d\n", i);
            return ret;
        }
    }

    /* Enable clock */
    ret = clk_prepare_enable(spi->clk);
    if (ret) {
        dev_err(dev, "Failed to enable clock\n");
        return ret;
    }

    /* Initialize hardware */
    ret = custom_spi_hw_init(spi);
    if (ret)
        goto err_disable_clk;

    /* Request IRQ */
    ret = devm_request_irq(dev, spi->irq, custom_spi_irq, 0,
                           dev_name(dev), spi);
    if (ret) {
        dev_err(dev, "Failed to request IRQ\n");
        goto err_disable_clk;
    }

    /* Initialize DMA */
    ret = custom_spi_dma_init(spi);
    if (ret)
        goto err_disable_clk;

    /* Configure SPI controller */
    ctlr->bus_num = pdev->id;
    ctlr->num_chipselect = spi->num_cs;
    ctlr->mode_bits = SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_LSB_FIRST;
    ctlr->bits_per_word_mask = SPI_BPW_RANGE_MASK(4, 16);
    ctlr->min_speed_hz = clk_get_rate(spi->clk) / (254 * 256);
    ctlr->max_speed_hz = clk_get_rate(spi->clk) / 2;
    ctlr->prepare_message = custom_spi_prepare_message;
    ctlr->transfer_one = custom_spi_transfer_one;
    ctlr->unprepare_message = custom_spi_unprepare_message;
    ctlr->set_cs = custom_spi_set_cs;
    ctlr->can_dma = custom_spi_can_dma;
    ctlr->dev.of_node = dev->of_node;

    platform_set_drvdata(pdev, spi);

    /* Register SPI controller */
    ret = devm_spi_register_controller(dev, ctlr);
    if (ret) {
        dev_err(dev, "Failed to register SPI controller\n");
        goto err_dma_release;
    }

    dev_info(dev, "SPI controller registered, %d CS, %s\n",
             spi->num_cs, spi->dma_enabled ? "DMA" : "PIO");

    return 0;

err_dma_release:
    custom_spi_dma_release(spi);
err_disable_clk:
    clk_disable_unprepare(spi->clk);
    return ret;
}

static int custom_spi_remove(struct platform_device *pdev)
{
    struct custom_spi *spi = platform_get_drvdata(pdev);

    custom_spi_disable(spi);
    custom_spi_dma_release(spi);
    clk_disable_unprepare(spi->clk);

    dev_info(spi->dev, "Stats: %lu transfers, %lu TX bytes, %lu RX bytes, %lu errors\n",
             spi->transfers, spi->bytes_tx, spi->bytes_rx, spi->errors);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int custom_spi_suspend(struct device *dev)
{
    struct custom_spi *spi = dev_get_drvdata(dev);

    spi_controller_suspend(spi->controller);
    custom_spi_disable(spi);
    clk_disable_unprepare(spi->clk);

    return 0;
}

static int custom_spi_resume(struct device *dev)
{
    struct custom_spi *spi = dev_get_drvdata(dev);
    int ret;

    ret = clk_prepare_enable(spi->clk);
    if (ret)
        return ret;

    custom_spi_hw_init(spi);
    spi_controller_resume(spi->controller);

    return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(custom_spi_pm_ops,
                         custom_spi_suspend, custom_spi_resume);

static const struct of_device_id custom_spi_of_match[] = {
    { .compatible = "vendor,custom-spi" },
    { }
};
MODULE_DEVICE_TABLE(of, custom_spi_of_match);

static struct platform_driver custom_spi_driver = {
    .probe  = custom_spi_probe,
    .remove = custom_spi_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = custom_spi_of_match,
        .pm = &custom_spi_pm_ops,
    },
};
module_platform_driver(custom_spi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("Full-Featured SPI Controller Driver with DMA");
MODULE_VERSION("1.0");
