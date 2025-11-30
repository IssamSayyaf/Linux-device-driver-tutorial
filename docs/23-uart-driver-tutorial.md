# Custom UART Controller Driver Tutorial

Complete guide to implementing a full-featured UART/Serial controller driver.

## What This Driver Demonstrates

| Feature | Implementation | Complexity |
|---------|---------------|------------|
| TTY/Serial Core | `uart_driver` + `uart_port` | Advanced |
| Interrupt TX/RX | IRQ handler with FIFO | Intermediate |
| DMA Support | Scatter-gather DMA | Advanced |
| Console Support | Early printk | Advanced |
| RS-485 Mode | Direction control | Intermediate |
| Hardware Flow Control | RTS/CTS | Intermediate |
| Power Management | Runtime PM | Intermediate |

---

## UART Subsystem Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                         USER SPACE                                   │
│                                                                      │
│   Application ←──→ /dev/ttyXXX0  (via open/read/write/ioctl)       │
│   minicom, screen, etc.                                             │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                        TTY LAYER                                     │
│                                                                      │
│   ┌─────────────┐    ┌─────────────┐    ┌─────────────────────────┐│
│   │ TTY Core    │◄──►│Line Discip. │◄──►│   TTY Driver            ││
│   │ (tty_io.c)  │    │ (N_TTY)     │    │   (uart_driver)         ││
│   └─────────────┘    └─────────────┘    └───────────┬─────────────┘│
└─────────────────────────────────────────────────────┼───────────────┘
                                                      │
                                                      ▼
┌─────────────────────────────────────────────────────────────────────┐
│                      SERIAL CORE                                     │
│                                                                      │
│   struct uart_driver ──→ uart_register_driver()                     │
│   struct uart_port   ──→ uart_add_one_port()                        │
│   struct uart_ops    ──→ Hardware operations                        │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    CUSTOM UART DRIVER                                │
│                                                                      │
│   uart_ops:                                                          │
│   ├─ .startup()      ──→ Enable clocks, request IRQ                 │
│   ├─ .shutdown()     ──→ Disable, free resources                    │
│   ├─ .start_tx()     ──→ Begin transmission                         │
│   ├─ .stop_tx()      ──→ Stop transmission                          │
│   ├─ .stop_rx()      ──→ Stop reception                             │
│   ├─ .set_termios()  ──→ Configure baud, parity, etc.               │
│   ├─ .tx_empty()     ──→ Check if TX complete                       │
│   └─ .set_mctrl()    ──→ Set modem control (RTS, DTR)               │
└───────────────────────────────┬─────────────────────────────────────┘
                                │
                                ▼
┌─────────────────────────────────────────────────────────────────────┐
│                    HARDWARE (Memory-Mapped UART)                     │
│                                                                      │
│   Registers:                                                         │
│   ├─ RBR/THR (0x00) ──→ Receive/Transmit data                       │
│   ├─ IER (0x04)     ──→ Interrupt enable                            │
│   ├─ IIR/FCR (0x08) ──→ IRQ status / FIFO control                   │
│   ├─ LCR (0x0C)     ──→ Line control (data bits, parity, stop)      │
│   ├─ MCR (0x10)     ──→ Modem control (RTS, DTR, loopback)          │
│   ├─ LSR (0x14)     ──→ Line status (TX empty, RX ready, errors)    │
│   └─ MSR (0x18)     ──→ Modem status (CTS, DSR, RI, DCD)            │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Key Code Patterns

### 1. UART Driver Registration

```c
/* Define the uart_driver */
static struct uart_driver custom_uart_driver = {
    .owner          = THIS_MODULE,
    .driver_name    = "custom_uart",
    .dev_name       = "ttyXXX",       /* Creates /dev/ttyXXX0, etc. */
    .major          = 0,              /* Auto-assign major number */
    .minor          = 0,
    .nr             = UART_NR,        /* Max number of ports */
    .cons           = CUSTOM_CONSOLE, /* Console (optional) */
};

/* In probe(): */
ret = uart_register_driver(&custom_uart_driver);
ret = uart_add_one_port(&custom_uart_driver, &port->port);
```

### 2. UART Operations

```c
static const struct uart_ops custom_uart_ops = {
    /* TX/RX control */
    .start_tx       = custom_uart_start_tx,
    .stop_tx        = custom_uart_stop_tx,
    .stop_rx        = custom_uart_stop_rx,

    /* Port management */
    .startup        = custom_uart_startup,
    .shutdown       = custom_uart_shutdown,

    /* Configuration */
    .set_termios    = custom_uart_set_termios,
    .set_mctrl      = custom_uart_set_mctrl,
    .get_mctrl      = custom_uart_get_mctrl,

    /* Status */
    .tx_empty       = custom_uart_tx_empty,
    .type           = custom_uart_type,
};
```

### 3. Interrupt Handler

```c
static irqreturn_t custom_uart_irq(int irq, void *dev_id)
{
    struct custom_uart_port *port = dev_id;
    u32 iir = uart_read(port, UART_IIR);

    if (iir & UART_IIR_NO_INT)
        return IRQ_NONE;

    switch (iir & UART_IIR_ID_MASK) {
    case UART_IIR_RDI:      /* Receive data available */
        custom_uart_rx_chars(port);
        break;

    case UART_IIR_THRI:     /* TX holding register empty */
        custom_uart_tx_chars(port);
        break;

    case UART_IIR_RLSI:     /* Receiver line status */
        custom_uart_handle_error(port);
        break;
    }

    return IRQ_HANDLED;
}
```

### 4. Receiving Characters

```c
static void custom_uart_rx_chars(struct custom_uart_port *port)
{
    struct uart_port *uport = &port->port;
    u32 lsr;
    u8 ch;

    while ((lsr = uart_read(port, UART_LSR)) & UART_LSR_DR) {
        ch = uart_read(port, UART_RBR);

        /* Check for errors */
        if (lsr & UART_LSR_OE)
            uport->icount.overrun++;
        if (lsr & UART_LSR_PE)
            uport->icount.parity++;

        /* Pass to TTY layer */
        uart_insert_char(uport, lsr, UART_LSR_OE, ch, TTY_NORMAL);
    }

    /* Push to TTY buffer */
    tty_flip_buffer_push(&uport->state->port);
}
```

### 5. Transmitting Characters

```c
static void custom_uart_tx_chars(struct custom_uart_port *port)
{
    struct uart_port *uport = &port->port;
    struct circ_buf *xmit = &uport->state->xmit;

    if (uart_circ_empty(xmit)) {
        custom_uart_stop_tx(uport);
        return;
    }

    while (!uart_circ_empty(xmit)) {
        /* Check if TX FIFO has room */
        if (!(uart_read(port, UART_LSR) & UART_LSR_THRE))
            break;

        uart_write(port, UART_THR, xmit->buf[xmit->tail]);
        xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
        uport->icount.tx++;
    }

    if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
        uart_write_wakeup(uport);
}
```

### 6. Setting Baud Rate (set_termios)

```c
static void custom_uart_set_termios(struct uart_port *uport,
                                    struct ktermios *termios,
                                    const struct ktermios *old)
{
    unsigned int baud;
    u32 lcr = 0;

    /* Calculate baud rate */
    baud = uart_get_baud_rate(uport, termios, old, 9600, 921600);

    /* Set data bits */
    switch (termios->c_cflag & CSIZE) {
    case CS5: lcr |= UART_LCR_WLEN5; break;
    case CS6: lcr |= UART_LCR_WLEN6; break;
    case CS7: lcr |= UART_LCR_WLEN7; break;
    case CS8: lcr |= UART_LCR_WLEN8; break;
    }

    /* Set stop bits */
    if (termios->c_cflag & CSTOPB)
        lcr |= UART_LCR_STOP;

    /* Set parity */
    if (termios->c_cflag & PARENB) {
        lcr |= UART_LCR_PARITY;
        if (!(termios->c_cflag & PARODD))
            lcr |= UART_LCR_EPAR;
    }

    /* Program divisor latch */
    uart_write(port, UART_LCR, lcr | UART_LCR_DLAB);
    uart_write(port, UART_DLL, divisor & 0xFF);
    uart_write(port, UART_DLH, (divisor >> 8) & 0xFF);
    uart_write(port, UART_LCR, lcr);
}
```

---

## DMA Support Pattern

```c
#ifdef CONFIG_SERIAL_CUSTOM_UART_DMA
static void custom_uart_dma_rx_complete(void *arg)
{
    struct custom_uart_port *port = arg;

    /* Copy from DMA buffer to TTY */
    tty_insert_flip_string(&port->port.state->port,
                           port->rx_dma.buf,
                           port->rx_dma.count);
    tty_flip_buffer_push(&port->port.state->port);

    /* Restart DMA */
    custom_uart_start_rx_dma(port);
}
#endif
```

---

## Console Support

```c
#ifdef CONFIG_SERIAL_CUSTOM_UART_CONSOLE
static void custom_uart_console_write(struct console *co,
                                      const char *s, unsigned count)
{
    struct custom_uart_port *port = &custom_uart_ports[co->index];

    /* Busy-wait TX (console can't use interrupts early in boot) */
    while (count--) {
        while (!(uart_read(port, UART_LSR) & UART_LSR_THRE))
            cpu_relax();
        uart_write(port, UART_THR, *s++);
    }
}

static struct console custom_uart_console = {
    .name   = "ttyXXX",
    .write  = custom_uart_console_write,
    .setup  = custom_uart_console_setup,
    .flags  = CON_PRINTBUFFER,
    .index  = -1,
};
#endif
```

---

## Device Tree

```dts
uart0: serial@10000000 {
    compatible = "vendor,custom-uart";
    reg = <0x10000000 0x100>;
    interrupts = <GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&uart_clk>;
    clock-names = "uart";

    /* Optional DMA */
    dmas = <&dma 0>, <&dma 1>;
    dma-names = "rx", "tx";

    status = "okay";
};
```

---

## Key Learning Points

1. **Serial Core API** - Handles TTY complexity for you
2. **FIFO Management** - Balance between latency and CPU overhead
3. **IRQ Design** - Handle TX/RX/errors in same handler
4. **DMA vs PIO** - DMA for high throughput, PIO for low latency
5. **Console Boot** - Must work before interrupts are available
