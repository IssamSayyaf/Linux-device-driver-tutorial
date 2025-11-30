/*
 * custom_uart.c - Full-Featured UART Controller Driver
 *
 * This is a complete UART/Serial controller driver demonstrating:
 * - TTY subsystem integration
 * - Serial core framework
 * - Interrupt-driven TX/RX
 * - DMA support
 * - Hardware flow control (RTS/CTS)
 * - Console support
 * - Power management
 *
 * Copyright (C) 2024
 * Licensed under GPL v2
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/console.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <linux/pm_runtime.h>

#define DRIVER_NAME     "custom_uart"
#define UART_NR         4       /* Maximum number of UARTs */
#define FIFO_SIZE       64      /* Hardware FIFO depth */

/*
 * ============================================================
 * Register Definitions (Example: Similar to 16550/8250)
 * ============================================================
 */

/* Register offsets */
#define UART_RBR        0x00    /* Receive Buffer Register (read) */
#define UART_THR        0x00    /* Transmit Holding Register (write) */
#define UART_IER        0x04    /* Interrupt Enable Register */
#define UART_IIR        0x08    /* Interrupt Identification Register (read) */
#define UART_FCR        0x08    /* FIFO Control Register (write) */
#define UART_LCR        0x0C    /* Line Control Register */
#define UART_MCR        0x10    /* Modem Control Register */
#define UART_LSR        0x14    /* Line Status Register */
#define UART_MSR        0x18    /* Modem Status Register */
#define UART_SCR        0x1C    /* Scratch Register */
#define UART_DLL        0x00    /* Divisor Latch Low (LCR[7]=1) */
#define UART_DLH        0x04    /* Divisor Latch High (LCR[7]=1) */

/* IER bits */
#define UART_IER_ERBFI  BIT(0)  /* Enable Received Data Available Interrupt */
#define UART_IER_ETBEI  BIT(1)  /* Enable Transmitter Holding Empty Interrupt */
#define UART_IER_ELSI   BIT(2)  /* Enable Receiver Line Status Interrupt */
#define UART_IER_EDSSI  BIT(3)  /* Enable Modem Status Interrupt */

/* IIR bits */
#define UART_IIR_NO_INT     BIT(0)
#define UART_IIR_ID_MASK    0x0E
#define UART_IIR_MSI        0x00    /* Modem status interrupt */
#define UART_IIR_THRI       0x02    /* Transmitter holding register empty */
#define UART_IIR_RDI        0x04    /* Receiver data interrupt */
#define UART_IIR_RLSI       0x06    /* Receiver line status interrupt */
#define UART_IIR_TIMEOUT    0x0C    /* Character timeout */

/* FCR bits */
#define UART_FCR_ENABLE_FIFO    BIT(0)
#define UART_FCR_CLEAR_RCVR     BIT(1)
#define UART_FCR_CLEAR_XMIT     BIT(2)
#define UART_FCR_DMA_SELECT     BIT(3)
#define UART_FCR_TRIGGER_1      0x00
#define UART_FCR_TRIGGER_4      0x40
#define UART_FCR_TRIGGER_8      0x80
#define UART_FCR_TRIGGER_14     0xC0

/* LCR bits */
#define UART_LCR_WLEN5      0x00
#define UART_LCR_WLEN6      0x01
#define UART_LCR_WLEN7      0x02
#define UART_LCR_WLEN8      0x03
#define UART_LCR_STOP       BIT(2)  /* 2 stop bits */
#define UART_LCR_PARITY     BIT(3)  /* Parity enable */
#define UART_LCR_EPAR       BIT(4)  /* Even parity */
#define UART_LCR_SPAR       BIT(5)  /* Stick parity */
#define UART_LCR_SBC        BIT(6)  /* Set break */
#define UART_LCR_DLAB       BIT(7)  /* Divisor latch access */

/* MCR bits */
#define UART_MCR_DTR        BIT(0)
#define UART_MCR_RTS        BIT(1)
#define UART_MCR_OUT1       BIT(2)
#define UART_MCR_OUT2       BIT(3)
#define UART_MCR_LOOP       BIT(4)
#define UART_MCR_AFE        BIT(5)  /* Auto flow control enable */

/* LSR bits */
#define UART_LSR_DR         BIT(0)  /* Data ready */
#define UART_LSR_OE         BIT(1)  /* Overrun error */
#define UART_LSR_PE         BIT(2)  /* Parity error */
#define UART_LSR_FE         BIT(3)  /* Framing error */
#define UART_LSR_BI         BIT(4)  /* Break interrupt */
#define UART_LSR_THRE       BIT(5)  /* Transmit holding register empty */
#define UART_LSR_TEMT       BIT(6)  /* Transmitter empty */
#define UART_LSR_FIFOE      BIT(7)  /* Error in FIFO */

/* MSR bits */
#define UART_MSR_DCTS       BIT(0)  /* Delta CTS */
#define UART_MSR_DDSR       BIT(1)  /* Delta DSR */
#define UART_MSR_TERI       BIT(2)  /* Trailing edge ring indicator */
#define UART_MSR_DDCD       BIT(3)  /* Delta DCD */
#define UART_MSR_CTS        BIT(4)  /* CTS */
#define UART_MSR_DSR        BIT(5)  /* DSR */
#define UART_MSR_RI         BIT(6)  /* Ring indicator */
#define UART_MSR_DCD        BIT(7)  /* DCD */

/*
 * ============================================================
 * Driver Data Structures
 * ============================================================
 */

struct custom_uart_dma {
    struct dma_chan         *chan;
    struct dma_async_tx_descriptor *desc;
    dma_addr_t              dma_addr;
    void                    *buf;
    size_t                  buf_size;
    dma_cookie_t            cookie;
    bool                    running;
};

struct custom_uart_port {
    struct uart_port        port;
    struct clk              *clk;
    unsigned int            ier;            /* Cached IER value */
    unsigned int            mcr;            /* Cached MCR value */
    unsigned int            lcr;            /* Cached LCR value */

    /* DMA support */
    bool                    dma_enabled;
    struct custom_uart_dma  tx_dma;
    struct custom_uart_dma  rx_dma;

    /* Flags */
    bool                    console_enabled;
    bool                    rs485_enabled;

    /* Statistics */
    unsigned long           rx_bytes;
    unsigned long           tx_bytes;
    unsigned long           rx_errors;
};

/* Port array */
static struct custom_uart_port custom_uart_ports[UART_NR];

/*
 * ============================================================
 * Register Access Functions
 * ============================================================
 */

static inline u32 uart_read(struct custom_uart_port *up, int offset)
{
    return readl(up->port.membase + offset);
}

static inline void uart_write(struct custom_uart_port *up, int offset, u32 value)
{
    writel(value, up->port.membase + offset);
}

/*
 * ============================================================
 * Hardware Control Functions
 * ============================================================
 */

static void custom_uart_set_ier(struct custom_uart_port *up, unsigned int ier)
{
    up->ier = ier;
    uart_write(up, UART_IER, ier);
}

static void custom_uart_set_mcr(struct custom_uart_port *up, unsigned int mcr)
{
    up->mcr = mcr;
    uart_write(up, UART_MCR, mcr);
}

static unsigned int custom_uart_get_divisor(struct custom_uart_port *up,
                                            unsigned int baud)
{
    unsigned int clk_rate;

    clk_rate = clk_get_rate(up->clk);
    if (!clk_rate)
        clk_rate = up->port.uartclk;

    return DIV_ROUND_CLOSEST(clk_rate, 16 * baud);
}

static void custom_uart_set_baud(struct custom_uart_port *up, unsigned int baud)
{
    unsigned int divisor;

    divisor = custom_uart_get_divisor(up, baud);

    /* Enable divisor latch access */
    uart_write(up, UART_LCR, up->lcr | UART_LCR_DLAB);

    /* Set divisor */
    uart_write(up, UART_DLL, divisor & 0xFF);
    uart_write(up, UART_DLH, (divisor >> 8) & 0xFF);

    /* Disable divisor latch access */
    uart_write(up, UART_LCR, up->lcr);
}

/*
 * ============================================================
 * FIFO Management
 * ============================================================
 */

static void custom_uart_enable_fifo(struct custom_uart_port *up)
{
    uart_write(up, UART_FCR,
               UART_FCR_ENABLE_FIFO |
               UART_FCR_CLEAR_RCVR |
               UART_FCR_CLEAR_XMIT |
               UART_FCR_TRIGGER_8);
}

static void custom_uart_disable_fifo(struct custom_uart_port *up)
{
    uart_write(up, UART_FCR, 0);
}

static void custom_uart_clear_fifos(struct custom_uart_port *up)
{
    uart_write(up, UART_FCR,
               UART_FCR_ENABLE_FIFO |
               UART_FCR_CLEAR_RCVR |
               UART_FCR_CLEAR_XMIT);
}

/*
 * ============================================================
 * TX Functions
 * ============================================================
 */

static void custom_uart_start_tx_pio(struct custom_uart_port *up)
{
    /* Enable TX interrupt */
    if (!(up->ier & UART_IER_ETBEI)) {
        up->ier |= UART_IER_ETBEI;
        uart_write(up, UART_IER, up->ier);
    }
}

static void custom_uart_stop_tx_pio(struct custom_uart_port *up)
{
    /* Disable TX interrupt */
    if (up->ier & UART_IER_ETBEI) {
        up->ier &= ~UART_IER_ETBEI;
        uart_write(up, UART_IER, up->ier);
    }
}

static void custom_uart_tx_chars(struct custom_uart_port *up)
{
    struct circ_buf *xmit = &up->port.state->xmit;
    int count;

    if (up->port.x_char) {
        uart_write(up, UART_THR, up->port.x_char);
        up->port.icount.tx++;
        up->port.x_char = 0;
        return;
    }

    if (uart_circ_empty(xmit) || uart_tx_stopped(&up->port)) {
        custom_uart_stop_tx_pio(up);
        return;
    }

    /* Fill FIFO */
    count = FIFO_SIZE;
    while (count > 0 && !uart_circ_empty(xmit)) {
        uart_write(up, UART_THR, xmit->buf[xmit->tail]);
        xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
        up->port.icount.tx++;
        up->tx_bytes++;
        count--;
    }

    if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
        uart_write_wakeup(&up->port);

    if (uart_circ_empty(xmit))
        custom_uart_stop_tx_pio(up);
}

/*
 * ============================================================
 * RX Functions
 * ============================================================
 */

static void custom_uart_rx_chars(struct custom_uart_port *up)
{
    struct tty_port *tport = &up->port.state->port;
    unsigned int lsr, ch, flag;
    int max_count = 256;

    lsr = uart_read(up, UART_LSR);

    while ((lsr & UART_LSR_DR) && max_count--) {
        ch = uart_read(up, UART_RBR);
        flag = TTY_NORMAL;
        up->port.icount.rx++;
        up->rx_bytes++;

        /* Check for errors */
        if (unlikely(lsr & (UART_LSR_BI | UART_LSR_PE |
                           UART_LSR_FE | UART_LSR_OE))) {
            up->rx_errors++;

            if (lsr & UART_LSR_BI) {
                lsr &= ~(UART_LSR_FE | UART_LSR_PE);
                up->port.icount.brk++;
                if (uart_handle_break(&up->port))
                    goto next_char;
            } else if (lsr & UART_LSR_PE) {
                up->port.icount.parity++;
            } else if (lsr & UART_LSR_FE) {
                up->port.icount.frame++;
            }

            if (lsr & UART_LSR_OE)
                up->port.icount.overrun++;

            /* Mask errors based on termios */
            lsr &= up->port.read_status_mask;

            if (lsr & UART_LSR_BI)
                flag = TTY_BREAK;
            else if (lsr & UART_LSR_PE)
                flag = TTY_PARITY;
            else if (lsr & UART_LSR_FE)
                flag = TTY_FRAME;
        }

        if (uart_handle_sysrq_char(&up->port, ch))
            goto next_char;

        uart_insert_char(&up->port, lsr, UART_LSR_OE, ch, flag);

next_char:
        lsr = uart_read(up, UART_LSR);
    }

    tty_flip_buffer_push(tport);
}

/*
 * ============================================================
 * DMA Functions
 * ============================================================
 */

#define DMA_BUF_SIZE    4096

static void custom_uart_tx_dma_callback(void *data)
{
    struct custom_uart_port *up = data;
    struct circ_buf *xmit = &up->port.state->xmit;
    unsigned long flags;

    spin_lock_irqsave(&up->port.lock, flags);

    up->tx_dma.running = false;

    /* Update tail pointer */
    xmit->tail = (xmit->tail + up->tx_dma.buf_size) & (UART_XMIT_SIZE - 1);
    up->port.icount.tx += up->tx_dma.buf_size;

    if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
        uart_write_wakeup(&up->port);

    /* Start next DMA if more data */
    if (!uart_circ_empty(xmit) && !uart_tx_stopped(&up->port)) {
        /* Schedule next DMA transfer */
        /* ... */
    }

    spin_unlock_irqrestore(&up->port.lock, flags);
}

static int custom_uart_start_tx_dma(struct custom_uart_port *up)
{
    struct circ_buf *xmit = &up->port.state->xmit;
    struct dma_async_tx_descriptor *desc;
    size_t count;

    if (up->tx_dma.running)
        return 0;

    count = CIRC_CNT_TO_END(xmit->head, xmit->tail, UART_XMIT_SIZE);
    if (!count)
        return 0;

    /* Limit to DMA buffer size */
    if (count > DMA_BUF_SIZE)
        count = DMA_BUF_SIZE;

    /* Copy data to DMA buffer */
    memcpy(up->tx_dma.buf, &xmit->buf[xmit->tail], count);
    up->tx_dma.buf_size = count;

    /* Sync DMA buffer */
    dma_sync_single_for_device(up->port.dev, up->tx_dma.dma_addr,
                               count, DMA_TO_DEVICE);

    /* Prepare DMA transfer */
    desc = dmaengine_prep_slave_single(up->tx_dma.chan,
                                       up->tx_dma.dma_addr,
                                       count,
                                       DMA_MEM_TO_DEV,
                                       DMA_PREP_INTERRUPT);
    if (!desc)
        return -ENOMEM;

    desc->callback = custom_uart_tx_dma_callback;
    desc->callback_param = up;

    up->tx_dma.cookie = dmaengine_submit(desc);
    if (dma_submit_error(up->tx_dma.cookie))
        return -ENOMEM;

    dma_async_issue_pending(up->tx_dma.chan);
    up->tx_dma.running = true;

    return 0;
}

static void custom_uart_rx_dma_callback(void *data)
{
    struct custom_uart_port *up = data;
    struct tty_port *tport = &up->port.state->port;
    struct dma_tx_state state;
    size_t count;
    unsigned long flags;

    spin_lock_irqsave(&up->port.lock, flags);

    dmaengine_tx_status(up->rx_dma.chan, up->rx_dma.cookie, &state);
    count = DMA_BUF_SIZE - state.residue;

    if (count) {
        /* Sync DMA buffer */
        dma_sync_single_for_cpu(up->port.dev, up->rx_dma.dma_addr,
                                count, DMA_FROM_DEVICE);

        /* Push to TTY */
        tty_insert_flip_string(tport, up->rx_dma.buf, count);
        up->port.icount.rx += count;
        up->rx_bytes += count;

        tty_flip_buffer_push(tport);
    }

    /* Restart DMA */
    /* ... */

    spin_unlock_irqrestore(&up->port.lock, flags);
}

static int custom_uart_init_dma(struct custom_uart_port *up)
{
    struct device *dev = up->port.dev;
    int ret;

    /* Request TX DMA channel */
    up->tx_dma.chan = dma_request_chan(dev, "tx");
    if (IS_ERR(up->tx_dma.chan)) {
        ret = PTR_ERR(up->tx_dma.chan);
        up->tx_dma.chan = NULL;
        if (ret != -ENODEV && ret != -EPROBE_DEFER)
            dev_warn(dev, "TX DMA not available: %d\n", ret);
        goto no_dma;
    }

    /* Request RX DMA channel */
    up->rx_dma.chan = dma_request_chan(dev, "rx");
    if (IS_ERR(up->rx_dma.chan)) {
        ret = PTR_ERR(up->rx_dma.chan);
        up->rx_dma.chan = NULL;
        dev_warn(dev, "RX DMA not available: %d\n", ret);
        dma_release_channel(up->tx_dma.chan);
        up->tx_dma.chan = NULL;
        goto no_dma;
    }

    /* Allocate DMA buffers */
    up->tx_dma.buf = dma_alloc_coherent(dev, DMA_BUF_SIZE,
                                        &up->tx_dma.dma_addr, GFP_KERNEL);
    if (!up->tx_dma.buf) {
        ret = -ENOMEM;
        goto err_free_chan;
    }

    up->rx_dma.buf = dma_alloc_coherent(dev, DMA_BUF_SIZE,
                                        &up->rx_dma.dma_addr, GFP_KERNEL);
    if (!up->rx_dma.buf) {
        ret = -ENOMEM;
        goto err_free_tx_buf;
    }

    up->dma_enabled = true;
    dev_info(dev, "DMA enabled for TX and RX\n");
    return 0;

err_free_tx_buf:
    dma_free_coherent(dev, DMA_BUF_SIZE, up->tx_dma.buf, up->tx_dma.dma_addr);
err_free_chan:
    dma_release_channel(up->rx_dma.chan);
    dma_release_channel(up->tx_dma.chan);
    up->tx_dma.chan = NULL;
    up->rx_dma.chan = NULL;
no_dma:
    up->dma_enabled = false;
    return 0;  /* Continue without DMA */
}

static void custom_uart_release_dma(struct custom_uart_port *up)
{
    struct device *dev = up->port.dev;

    if (!up->dma_enabled)
        return;

    if (up->tx_dma.chan) {
        dmaengine_terminate_sync(up->tx_dma.chan);
        dma_free_coherent(dev, DMA_BUF_SIZE,
                          up->tx_dma.buf, up->tx_dma.dma_addr);
        dma_release_channel(up->tx_dma.chan);
    }

    if (up->rx_dma.chan) {
        dmaengine_terminate_sync(up->rx_dma.chan);
        dma_free_coherent(dev, DMA_BUF_SIZE,
                          up->rx_dma.buf, up->rx_dma.dma_addr);
        dma_release_channel(up->rx_dma.chan);
    }

    up->dma_enabled = false;
}

/*
 * ============================================================
 * Interrupt Handler
 * ============================================================
 */

static irqreturn_t custom_uart_irq(int irq, void *dev_id)
{
    struct custom_uart_port *up = dev_id;
    unsigned int iir, lsr;
    unsigned long flags;
    irqreturn_t ret = IRQ_NONE;

    spin_lock_irqsave(&up->port.lock, flags);

    iir = uart_read(up, UART_IIR);

    while (!(iir & UART_IIR_NO_INT)) {
        ret = IRQ_HANDLED;

        switch (iir & UART_IIR_ID_MASK) {
        case UART_IIR_RLSI:
            /* Line status interrupt */
            lsr = uart_read(up, UART_LSR);
            if (lsr & UART_LSR_DR)
                custom_uart_rx_chars(up);
            break;

        case UART_IIR_RDI:
        case UART_IIR_TIMEOUT:
            /* Received data available */
            custom_uart_rx_chars(up);
            break;

        case UART_IIR_THRI:
            /* Transmitter holding register empty */
            custom_uart_tx_chars(up);
            break;

        case UART_IIR_MSI:
            /* Modem status interrupt */
            /* Handle CTS/DSR/DCD changes */
            break;
        }

        iir = uart_read(up, UART_IIR);
    }

    spin_unlock_irqrestore(&up->port.lock, flags);
    return ret;
}

/*
 * ============================================================
 * UART Operations
 * ============================================================
 */

static unsigned int custom_uart_tx_empty(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned int lsr;

    lsr = uart_read(up, UART_LSR);
    return (lsr & UART_LSR_TEMT) ? TIOCSER_TEMT : 0;
}

static void custom_uart_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned int mcr = 0;

    if (mctrl & TIOCM_RTS)
        mcr |= UART_MCR_RTS;
    if (mctrl & TIOCM_DTR)
        mcr |= UART_MCR_DTR;
    if (mctrl & TIOCM_OUT1)
        mcr |= UART_MCR_OUT1;
    if (mctrl & TIOCM_OUT2)
        mcr |= UART_MCR_OUT2;
    if (mctrl & TIOCM_LOOP)
        mcr |= UART_MCR_LOOP;

    custom_uart_set_mcr(up, mcr);
}

static unsigned int custom_uart_get_mctrl(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned int msr, mctrl = 0;

    msr = uart_read(up, UART_MSR);

    if (msr & UART_MSR_CTS)
        mctrl |= TIOCM_CTS;
    if (msr & UART_MSR_DSR)
        mctrl |= TIOCM_DSR;
    if (msr & UART_MSR_RI)
        mctrl |= TIOCM_RI;
    if (msr & UART_MSR_DCD)
        mctrl |= TIOCM_CD;

    return mctrl;
}

static void custom_uart_stop_tx(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);

    if (up->dma_enabled && up->tx_dma.running)
        dmaengine_terminate_async(up->tx_dma.chan);

    custom_uart_stop_tx_pio(up);
}

static void custom_uart_start_tx(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);

    if (up->dma_enabled) {
        if (custom_uart_start_tx_dma(up) == 0)
            return;
        /* Fall back to PIO on DMA failure */
    }

    custom_uart_start_tx_pio(up);
}

static void custom_uart_stop_rx(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);

    up->ier &= ~UART_IER_ERBFI;
    uart_write(up, UART_IER, up->ier);
}

static void custom_uart_break_ctl(struct uart_port *port, int break_state)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned long flags;

    spin_lock_irqsave(&port->lock, flags);

    if (break_state)
        up->lcr |= UART_LCR_SBC;
    else
        up->lcr &= ~UART_LCR_SBC;

    uart_write(up, UART_LCR, up->lcr);

    spin_unlock_irqrestore(&port->lock, flags);
}

static int custom_uart_startup(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    int ret;

    /* Enable clock */
    ret = clk_prepare_enable(up->clk);
    if (ret)
        return ret;

    /* Initialize FIFO */
    custom_uart_enable_fifo(up);

    /* Clear any pending interrupts */
    uart_read(up, UART_LSR);
    uart_read(up, UART_RBR);
    uart_read(up, UART_IIR);
    uart_read(up, UART_MSR);

    /* Enable interrupts */
    up->ier = UART_IER_ERBFI | UART_IER_ELSI;
    uart_write(up, UART_IER, up->ier);

    return 0;
}

static void custom_uart_shutdown(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);

    /* Disable interrupts */
    up->ier = 0;
    uart_write(up, UART_IER, 0);

    /* Disable FIFO */
    custom_uart_disable_fifo(up);

    /* Disable clock */
    clk_disable_unprepare(up->clk);
}

static void custom_uart_set_termios(struct uart_port *port,
                                    struct ktermios *termios,
                                    const struct ktermios *old)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned int baud, lcr = 0;
    unsigned long flags;

    /* Word length */
    switch (termios->c_cflag & CSIZE) {
    case CS5:
        lcr = UART_LCR_WLEN5;
        break;
    case CS6:
        lcr = UART_LCR_WLEN6;
        break;
    case CS7:
        lcr = UART_LCR_WLEN7;
        break;
    case CS8:
    default:
        lcr = UART_LCR_WLEN8;
        break;
    }

    /* Stop bits */
    if (termios->c_cflag & CSTOPB)
        lcr |= UART_LCR_STOP;

    /* Parity */
    if (termios->c_cflag & PARENB) {
        lcr |= UART_LCR_PARITY;
        if (!(termios->c_cflag & PARODD))
            lcr |= UART_LCR_EPAR;
    }

    /* Calculate baud rate */
    baud = uart_get_baud_rate(port, termios, old, 9600, 4000000);

    spin_lock_irqsave(&port->lock, flags);

    /* Update timeout */
    uart_update_timeout(port, termios->c_cflag, baud);

    /* Set read status mask */
    port->read_status_mask = UART_LSR_OE;
    if (termios->c_iflag & INPCK)
        port->read_status_mask |= UART_LSR_PE | UART_LSR_FE;
    if (termios->c_iflag & (IGNBRK | BRKINT | PARMRK))
        port->read_status_mask |= UART_LSR_BI;

    /* Set ignore mask */
    port->ignore_status_mask = 0;
    if (termios->c_iflag & IGNPAR)
        port->ignore_status_mask |= UART_LSR_PE | UART_LSR_FE;
    if (termios->c_iflag & IGNBRK) {
        port->ignore_status_mask |= UART_LSR_BI;
        if (termios->c_iflag & IGNPAR)
            port->ignore_status_mask |= UART_LSR_OE;
    }

    /* Hardware flow control */
    if (termios->c_cflag & CRTSCTS) {
        up->mcr |= UART_MCR_AFE;
    } else {
        up->mcr &= ~UART_MCR_AFE;
    }

    up->lcr = lcr;
    uart_write(up, UART_LCR, lcr);
    custom_uart_set_baud(up, baud);
    custom_uart_set_mcr(up, up->mcr);

    spin_unlock_irqrestore(&port->lock, flags);
}

static const char *custom_uart_type(struct uart_port *port)
{
    return "CUSTOM_UART";
}

static void custom_uart_release_port(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);

    custom_uart_release_dma(up);

    if (port->membase)
        iounmap(port->membase);

    release_mem_region(port->mapbase, resource_size(port->memresource));
}

static int custom_uart_request_port(struct uart_port *port)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    int ret;

    if (!request_mem_region(port->mapbase,
                            resource_size(port->memresource),
                            DRIVER_NAME))
        return -EBUSY;

    port->membase = ioremap(port->mapbase,
                            resource_size(port->memresource));
    if (!port->membase) {
        release_mem_region(port->mapbase,
                           resource_size(port->memresource));
        return -ENOMEM;
    }

    ret = custom_uart_init_dma(up);
    if (ret) {
        iounmap(port->membase);
        release_mem_region(port->mapbase,
                           resource_size(port->memresource));
        return ret;
    }

    return 0;
}

static void custom_uart_config_port(struct uart_port *port, int flags)
{
    if (flags & UART_CONFIG_TYPE) {
        port->type = PORT_16550A;
        custom_uart_request_port(port);
    }
}

static int custom_uart_verify_port(struct uart_port *port,
                                   struct serial_struct *ser)
{
    if (ser->type != PORT_UNKNOWN && ser->type != PORT_16550A)
        return -EINVAL;
    if (ser->irq != port->irq)
        return -EINVAL;
    if (ser->io_type != UPIO_MEM)
        return -EINVAL;
    return 0;
}

static const struct uart_ops custom_uart_ops = {
    .tx_empty       = custom_uart_tx_empty,
    .set_mctrl      = custom_uart_set_mctrl,
    .get_mctrl      = custom_uart_get_mctrl,
    .stop_tx        = custom_uart_stop_tx,
    .start_tx       = custom_uart_start_tx,
    .stop_rx        = custom_uart_stop_rx,
    .break_ctl      = custom_uart_break_ctl,
    .startup        = custom_uart_startup,
    .shutdown       = custom_uart_shutdown,
    .set_termios    = custom_uart_set_termios,
    .type           = custom_uart_type,
    .release_port   = custom_uart_release_port,
    .request_port   = custom_uart_request_port,
    .config_port    = custom_uart_config_port,
    .verify_port    = custom_uart_verify_port,
};

/*
 * ============================================================
 * Console Support
 * ============================================================
 */

#ifdef CONFIG_SERIAL_CUSTOM_CONSOLE

static void custom_uart_console_putchar(struct uart_port *port, unsigned char ch)
{
    struct custom_uart_port *up = container_of(port, struct custom_uart_port, port);
    unsigned int timeout = 10000;

    /* Wait for FIFO space */
    while (!(uart_read(up, UART_LSR) & UART_LSR_THRE) && timeout--)
        cpu_relax();

    uart_write(up, UART_THR, ch);
}

static void custom_uart_console_write(struct console *co, const char *s,
                                      unsigned int count)
{
    struct custom_uart_port *up = &custom_uart_ports[co->index];
    unsigned int ier;
    unsigned long flags;

    spin_lock_irqsave(&up->port.lock, flags);

    /* Save and disable interrupts */
    ier = up->ier;
    uart_write(up, UART_IER, 0);

    uart_console_write(&up->port, s, count, custom_uart_console_putchar);

    /* Wait for transmitter to become empty */
    while (!(uart_read(up, UART_LSR) & UART_LSR_TEMT))
        cpu_relax();

    /* Restore interrupts */
    uart_write(up, UART_IER, ier);

    spin_unlock_irqrestore(&up->port.lock, flags);
}

static int custom_uart_console_setup(struct console *co, char *options)
{
    struct custom_uart_port *up;
    int baud = 115200;
    int bits = 8;
    int parity = 'n';
    int flow = 'n';

    if (co->index < 0 || co->index >= UART_NR)
        return -ENODEV;

    up = &custom_uart_ports[co->index];
    if (!up->port.membase)
        return -ENODEV;

    if (options)
        uart_parse_options(options, &baud, &parity, &bits, &flow);

    return uart_set_options(&up->port, co, baud, parity, bits, flow);
}

static struct uart_driver custom_uart_driver;

static struct console custom_uart_console = {
    .name       = "ttyCustom",
    .write      = custom_uart_console_write,
    .device     = uart_console_device,
    .setup      = custom_uart_console_setup,
    .flags      = CON_PRINTBUFFER,
    .index      = -1,
    .data       = &custom_uart_driver,
};

#define CUSTOM_UART_CONSOLE     (&custom_uart_console)

#else
#define CUSTOM_UART_CONSOLE     NULL
#endif /* CONFIG_SERIAL_CUSTOM_CONSOLE */

/*
 * ============================================================
 * UART Driver Structure
 * ============================================================
 */

static struct uart_driver custom_uart_driver = {
    .owner          = THIS_MODULE,
    .driver_name    = DRIVER_NAME,
    .dev_name       = "ttyCustom",
    .nr             = UART_NR,
    .cons           = CUSTOM_UART_CONSOLE,
};

/*
 * ============================================================
 * Platform Driver
 * ============================================================
 */

static int custom_uart_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct custom_uart_port *up;
    struct resource *res;
    int irq, ret;
    u32 port_id;

    /* Get port ID from device tree or platform data */
    ret = of_property_read_u32(dev->of_node, "port-id", &port_id);
    if (ret) {
        port_id = pdev->id;
        if (port_id < 0)
            port_id = 0;
    }

    if (port_id >= UART_NR) {
        dev_err(dev, "Invalid port ID: %u\n", port_id);
        return -EINVAL;
    }

    up = &custom_uart_ports[port_id];
    memset(up, 0, sizeof(*up));

    /* Get memory resource */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(dev, "No memory resource\n");
        return -ENODEV;
    }

    /* Get IRQ */
    irq = platform_get_irq(pdev, 0);
    if (irq < 0) {
        dev_err(dev, "No IRQ resource\n");
        return irq;
    }

    /* Get clock */
    up->clk = devm_clk_get(dev, NULL);
    if (IS_ERR(up->clk)) {
        dev_err(dev, "Failed to get clock\n");
        return PTR_ERR(up->clk);
    }

    /* Initialize uart_port structure */
    up->port.dev = dev;
    up->port.type = PORT_16550A;
    up->port.iotype = UPIO_MEM;
    up->port.mapbase = res->start;
    up->port.memresource = res;
    up->port.irq = irq;
    up->port.uartclk = clk_get_rate(up->clk);
    up->port.fifosize = FIFO_SIZE;
    up->port.ops = &custom_uart_ops;
    up->port.flags = UPF_BOOT_AUTOCONF;
    up->port.line = port_id;

    spin_lock_init(&up->port.lock);

    /* Map registers */
    up->port.membase = devm_ioremap_resource(dev, res);
    if (IS_ERR(up->port.membase))
        return PTR_ERR(up->port.membase);

    /* Request IRQ */
    ret = devm_request_irq(dev, irq, custom_uart_irq, 0,
                           dev_name(dev), up);
    if (ret) {
        dev_err(dev, "Failed to request IRQ: %d\n", ret);
        return ret;
    }

    /* Initialize DMA */
    custom_uart_init_dma(up);

    /* Add port to driver */
    ret = uart_add_one_port(&custom_uart_driver, &up->port);
    if (ret) {
        dev_err(dev, "Failed to add UART port: %d\n", ret);
        custom_uart_release_dma(up);
        return ret;
    }

    platform_set_drvdata(pdev, up);

    dev_info(dev, "UART%u registered at 0x%llx, IRQ %d%s\n",
             port_id, (unsigned long long)res->start, irq,
             up->dma_enabled ? " (DMA)" : "");

    return 0;
}

static int custom_uart_remove(struct platform_device *pdev)
{
    struct custom_uart_port *up = platform_get_drvdata(pdev);

    uart_remove_one_port(&custom_uart_driver, &up->port);
    custom_uart_release_dma(up);

    return 0;
}

#ifdef CONFIG_PM_SLEEP
static int custom_uart_suspend(struct device *dev)
{
    struct custom_uart_port *up = dev_get_drvdata(dev);

    uart_suspend_port(&custom_uart_driver, &up->port);
    clk_disable_unprepare(up->clk);

    return 0;
}

static int custom_uart_resume(struct device *dev)
{
    struct custom_uart_port *up = dev_get_drvdata(dev);

    clk_prepare_enable(up->clk);
    uart_resume_port(&custom_uart_driver, &up->port);

    return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(custom_uart_pm_ops,
                         custom_uart_suspend, custom_uart_resume);

static const struct of_device_id custom_uart_of_match[] = {
    { .compatible = "vendor,custom-uart" },
    { }
};
MODULE_DEVICE_TABLE(of, custom_uart_of_match);

static struct platform_driver custom_uart_platform_driver = {
    .probe  = custom_uart_probe,
    .remove = custom_uart_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = custom_uart_of_match,
        .pm = &custom_uart_pm_ops,
    },
};

/*
 * ============================================================
 * Module Init/Exit
 * ============================================================
 */

static int __init custom_uart_init(void)
{
    int ret;

    ret = uart_register_driver(&custom_uart_driver);
    if (ret)
        return ret;

    ret = platform_driver_register(&custom_uart_platform_driver);
    if (ret)
        uart_unregister_driver(&custom_uart_driver);

    return ret;
}

static void __exit custom_uart_exit(void)
{
    platform_driver_unregister(&custom_uart_platform_driver);
    uart_unregister_driver(&custom_uart_driver);
}

module_init(custom_uart_init);
module_exit(custom_uart_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Linux Driver Tutorial");
MODULE_DESCRIPTION("Full-Featured UART Controller Driver");
MODULE_VERSION("1.0");
