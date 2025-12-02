# Custom UART Controller Driver - UML Documentation

## Overview

The Custom UART driver is a full-featured UART/Serial controller driver that integrates with the Linux TTY and Serial Core subsystems. It demonstrates a complete implementation similar to 16550/8250 UART controllers.

**Driver Type:** Platform Device UART Controller Driver
**File:** `drivers/peripheral/uart/custom_uart.c`
**Subsystems:** TTY, Serial Core, DMA Engine, Clock, PM Runtime

---

## 1. Class Diagram (Data Structures)

```mermaid
classDiagram
    class custom_uart_port {
        +struct uart_port port
        +struct clk *clk
        +unsigned int ier
        +unsigned int mcr
        +unsigned int lcr
        +bool dma_enabled
        +struct custom_uart_dma tx_dma
        +struct custom_uart_dma rx_dma
        +bool console_enabled
        +bool rs485_enabled
        +struct serial_rs485 rs485
        +unsigned long rx_bytes
        +unsigned long tx_bytes
        +unsigned long rx_errors
    }

    class custom_uart_dma {
        +struct dma_chan *chan
        +struct dma_async_tx_descriptor *desc
        +dma_addr_t dma_addr
        +void *buf
        +size_t buf_size
        +dma_cookie_t cookie
        +bool running
    }

    class uart_port {
        +struct device *dev
        +unsigned int type
        +unsigned int iotype
        +resource_size_t mapbase
        +void __iomem *membase
        +unsigned int irq
        +unsigned int uartclk
        +unsigned int fifosize
        +const struct uart_ops *ops
        +unsigned int flags
        +unsigned int line
        +spinlock_t lock
    }

    class uart_ops {
        +tx_empty()
        +set_mctrl()
        +get_mctrl()
        +stop_tx()
        +start_tx()
        +stop_rx()
        +break_ctl()
        +startup()
        +shutdown()
        +set_termios()
        +type()
        +release_port()
        +request_port()
        +config_port()
        +verify_port()
    }

    class uart_driver {
        +struct module *owner
        +const char *driver_name
        +const char *dev_name
        +int nr
        +struct console *cons
    }

    class uart_state {
        +struct tty_port port
        +struct circ_buf xmit
    }

    custom_uart_port --> uart_port : contains
    custom_uart_port --> custom_uart_dma : tx_dma
    custom_uart_port --> custom_uart_dma : rx_dma
    uart_port --> uart_ops : ops
    uart_driver --> custom_uart_port : manages
    uart_port --> uart_state : state
```

---

## 2. Component Diagram (System Architecture)

```mermaid
flowchart TB
    subgraph UserSpace["User Space"]
        APP[Application]
        TTY_DEV["/dev/ttyCustomN"]
    end

    subgraph KernelSpace["Kernel Space"]
        subgraph TTY["TTY Subsystem"]
            TTY_CORE[TTY Core]
            LINE_DISC[Line Discipline]
            TTY_FLIP[Flip Buffer]
        end

        subgraph Serial["Serial Core"]
            SERIAL_CORE[Serial Core Framework]
            CONSOLE[Console Support]
        end

        subgraph Driver["Custom UART Driver"]
            UART_OPS[UART Operations]
            IRQ_HANDLER[Interrupt Handler]
            DMA_OPS[DMA Operations]
            RS485[RS-485 Support]
        end

        subgraph DMA["DMA Engine"]
            DMA_ENGINE[DMA Controller]
            DMA_CHAN[DMA Channels]
        end

        subgraph Platform["Platform Bus"]
            PLATFORM_DRV[Platform Driver]
            DT[Device Tree]
        end

        subgraph Clock["Clock Framework"]
            CLK[Clock Controller]
        end
    end

    subgraph Hardware["Hardware"]
        UART_HW[UART Controller]
        FIFO_TX[TX FIFO 64B]
        FIFO_RX[RX FIFO 64B]
        REGS[Register Bank]
    end

    APP --> TTY_DEV
    TTY_DEV --> TTY_CORE
    TTY_CORE --> LINE_DISC
    LINE_DISC --> SERIAL_CORE
    SERIAL_CORE --> UART_OPS
    CONSOLE --> UART_OPS
    IRQ_HANDLER --> TTY_FLIP
    TTY_FLIP --> TTY_CORE
    UART_OPS --> DMA_OPS
    DMA_OPS --> DMA_ENGINE
    DMA_ENGINE --> DMA_CHAN
    PLATFORM_DRV --> UART_OPS
    DT --> PLATFORM_DRV
    CLK --> UART_OPS
    UART_OPS --> REGS
    REGS --> FIFO_TX
    REGS --> FIFO_RX
    FIFO_TX --> UART_HW
    FIFO_RX --> UART_HW
```

---

## 3. Register Map Diagram

```
UART Register Map (16550 Compatible):

Offset  Name    R/W   Description
────────────────────────────────────────────────────────
0x00    RBR     R     Receive Buffer Register (DLAB=0)
0x00    THR     W     Transmit Holding Register (DLAB=0)
0x00    DLL     R/W   Divisor Latch Low (DLAB=1)
0x04    IER     R/W   Interrupt Enable Register (DLAB=0)
0x04    DLH     R/W   Divisor Latch High (DLAB=1)
0x08    IIR     R     Interrupt Identification Register
0x08    FCR     W     FIFO Control Register
0x0C    LCR     R/W   Line Control Register
0x10    MCR     R/W   Modem Control Register
0x14    LSR     R     Line Status Register
0x18    MSR     R     Modem Status Register
0x1C    SCR     R/W   Scratch Register

┌─────────────────────────────────────────────────────────┐
│                    IER (0x04)                           │
├─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────────────┤
│  7  │  6  │  5  │  4  │  3  │  2  │  1  │      0      │
├─────┴─────┴─────┴─────┼─────┼─────┼─────┼─────────────┤
│      Reserved         │EDSSI│ELSI │ETBEI│   ERBFI     │
│                       │Modem│Line │TX   │   RX Data   │
│                       │Stat │Stat │Empty│   Available │
└───────────────────────┴─────┴─────┴─────┴─────────────┘

┌─────────────────────────────────────────────────────────┐
│                    LSR (0x14)                           │
├─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────────────┤
│  7  │  6  │  5  │  4  │  3  │  2  │  1  │      0      │
├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────────────┤
│FIFOE│TEMT │THRE │ BI  │ FE  │ PE  │ OE  │     DR      │
│FIFO │TX   │TX   │Break│Frame│Par  │Over │   Data      │
│Error│Empty│Hold │Int  │Err  │Err  │run  │   Ready     │
└─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────────────┘

┌─────────────────────────────────────────────────────────┐
│                    LCR (0x0C)                           │
├─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────────────┤
│  7  │  6  │  5  │  4  │  3  │  2  │ 1-0             │
├─────┼─────┼─────┼─────┼─────┼─────┼─────────────────┤
│DLAB │SBC  │SPAR │EPAR │PEN  │STOP │    WLS          │
│Div  │Set  │Stick│Even │Par  │Stop │  Word Length    │
│Latch│Break│Par  │Par  │En   │Bits │  00=5, 11=8     │
└─────┴─────┴─────┴─────┴─────┴─────┴─────────────────┘
```

---

## 4. Interrupt State Machine

```mermaid
stateDiagram-v2
    [*] --> CheckIIR: IRQ Triggered

    CheckIIR --> HandleRLSI: IIR[3:1] = 011 (Line Status)
    CheckIIR --> HandleRDI: IIR[3:1] = 010 (RX Data)
    CheckIIR --> HandleTimeout: IIR[3:1] = 110 (Char Timeout)
    CheckIIR --> HandleTHRI: IIR[3:1] = 001 (TX Empty)
    CheckIIR --> HandleMSI: IIR[3:1] = 000 (Modem Status)
    CheckIIR --> Done: IIR[0] = 1 (No Interrupt)

    HandleRLSI --> ReadLSR: Read LSR register
    ReadLSR --> CheckDataReady: Check DR bit
    CheckDataReady --> HandleRDI: Data ready

    HandleRDI --> RxChars: custom_uart_rx_chars()
    note right of RxChars
        - Read from RBR
        - Check errors
        - Push to TTY flip buffer
    end note
    RxChars --> CheckIIR: Loop

    HandleTimeout --> RxChars

    HandleTHRI --> TxChars: custom_uart_tx_chars()
    note right of TxChars
        - Check x_char
        - Fill FIFO from xmit buffer
        - Call uart_write_wakeup()
    end note
    TxChars --> CheckStopTx: Check if empty
    CheckStopTx --> DisableTxIRQ: Buffer empty
    DisableTxIRQ --> CheckIIR
    CheckStopTx --> CheckIIR: More data

    HandleMSI --> ReadMSR: Handle modem signals
    ReadMSR --> CheckIIR

    Done --> [*]: Return IRQ_HANDLED/NONE
```

---

## 5. Sequence Diagram (TX Data Flow)

```mermaid
sequenceDiagram
    participant App as User Application
    participant TTY as TTY Layer
    participant Serial as Serial Core
    participant Driver as UART Driver
    participant DMA as DMA Engine
    participant HW as UART Hardware

    App->>TTY: write(fd, data, len)
    TTY->>Serial: uart_write()
    Serial->>Serial: Copy to xmit circular buffer

    alt DMA Enabled
        Serial->>Driver: custom_uart_start_tx()
        Driver->>Driver: custom_uart_rs485_start_tx()
        Driver->>Driver: custom_uart_start_tx_dma()
        Driver->>DMA: Copy data to DMA buffer
        Driver->>DMA: dmaengine_prep_slave_single()
        DMA-->>Driver: DMA descriptor
        Driver->>DMA: dmaengine_submit()
        Driver->>DMA: dma_async_issue_pending()

        DMA->>HW: Transfer data to THR
        HW-->>DMA: Transfer complete
        DMA->>Driver: custom_uart_tx_dma_callback()
        Driver->>Driver: Update xmit tail
        Driver->>Serial: uart_write_wakeup()

    else PIO Mode
        Serial->>Driver: custom_uart_start_tx()
        Driver->>Driver: custom_uart_rs485_start_tx()
        Driver->>Driver: Enable ETBEI interrupt
        Driver->>HW: Set IER[ETBEI]

        HW->>Driver: TX Empty IRQ
        Driver->>Driver: custom_uart_tx_chars()
        Driver->>HW: Write to THR (up to FIFO_SIZE)
        Driver->>Driver: Update xmit tail

        alt More data
            Driver->>Driver: Continue TX
        else Buffer empty
            Driver->>Driver: Disable ETBEI
            Driver->>Driver: custom_uart_rs485_stop_tx()
        end

        Driver->>Serial: uart_write_wakeup()
    end

    Serial-->>TTY: Return bytes written
    TTY-->>App: Return count
```

---

## 6. Sequence Diagram (RX Data Flow)

```mermaid
sequenceDiagram
    participant HW as UART Hardware
    participant IRQ as IRQ Handler
    participant Driver as UART Driver
    participant TTY as TTY Flip Buffer
    participant LD as Line Discipline
    participant App as User Application

    HW->>IRQ: RX Data Available IRQ
    IRQ->>Driver: custom_uart_irq()
    Driver->>Driver: Read IIR
    Driver->>Driver: custom_uart_rx_chars()

    loop While DR bit set (max 256)
        Driver->>HW: Read LSR
        Driver->>HW: Read RBR (data byte)

        alt Error bits set
            Driver->>Driver: Handle break/parity/frame/overrun
            Driver->>Driver: Update icount statistics
            Driver->>Driver: Determine TTY flag
        else No error
            Driver->>Driver: flag = TTY_NORMAL
        end

        alt SysRq character
            Driver->>Driver: uart_handle_sysrq_char()
        else Normal character
            Driver->>TTY: uart_insert_char()
        end
    end

    Driver->>TTY: tty_flip_buffer_push()

    TTY->>LD: Deliver data
    LD->>LD: Process according to discipline

    App->>LD: read(fd, buf, len)
    LD-->>App: Return data
```

---

## 7. Activity Diagram (Probe Function)

```mermaid
flowchart TD
    START([Start Probe]) --> GET_PORT_ID[Get port ID from DT/platform]
    GET_PORT_ID --> VALIDATE{Port ID valid?}
    VALIDATE -->|No| ERROR1[Return -EINVAL]
    VALIDATE -->|Yes| GET_MEM[Get memory resource]

    GET_MEM --> CHECK_MEM{Memory resource?}
    CHECK_MEM -->|No| ERROR2[Return -ENODEV]
    CHECK_MEM -->|Yes| GET_IRQ[Get IRQ resource]

    GET_IRQ --> CHECK_IRQ{IRQ valid?}
    CHECK_IRQ -->|No| ERROR3[Return error]
    CHECK_IRQ -->|Yes| GET_CLK[Get clock]

    GET_CLK --> CHECK_CLK{Clock available?}
    CHECK_CLK -->|No| ERROR4[Return error]
    CHECK_CLK -->|Yes| INIT_PORT[Initialize uart_port struct]

    INIT_PORT --> SET_TYPE[Set type = PORT_16550A]
    SET_TYPE --> SET_IOTYPE[Set iotype = UPIO_MEM]
    SET_IOTYPE --> SET_MAP[Set mapbase, irq, uartclk]
    SET_MAP --> SET_OPS[Set ops = &custom_uart_ops]
    SET_OPS --> INIT_LOCK[spin_lock_init()]

    INIT_LOCK --> IOREMAP[devm_ioremap_resource()]
    IOREMAP --> CHECK_MAP{Mapping OK?}
    CHECK_MAP -->|No| ERROR5[Return error]
    CHECK_MAP -->|Yes| REQ_IRQ[devm_request_irq()]

    REQ_IRQ --> CHECK_IRQREQ{IRQ request OK?}
    CHECK_IRQREQ -->|No| ERROR6[Return error]
    CHECK_IRQREQ -->|Yes| INIT_DMA[custom_uart_init_dma()]

    INIT_DMA --> ADD_PORT[uart_add_one_port()]
    ADD_PORT --> CHECK_ADD{Port added?}
    CHECK_ADD -->|No| RELEASE_DMA[Release DMA]
    RELEASE_DMA --> ERROR7[Return error]
    CHECK_ADD -->|Yes| LOG[dev_info: UART registered]

    LOG --> SUCCESS([Return 0])

    ERROR1 --> FAIL([Return error code])
    ERROR2 --> FAIL
    ERROR3 --> FAIL
    ERROR4 --> FAIL
    ERROR5 --> FAIL
    ERROR6 --> FAIL
    ERROR7 --> FAIL
```

---

## 8. DMA Transfer State Diagram

```mermaid
stateDiagram-v2
    [*] --> Idle: DMA Initialized

    Idle --> CheckData: start_tx() called
    CheckData --> Idle: No data in buffer
    CheckData --> PrepareTX: Data available

    PrepareTX --> CopyToBuffer: Copy from xmit buffer
    CopyToBuffer --> SyncBuffer: dma_sync_single_for_device()
    SyncBuffer --> PrepareDescriptor: dmaengine_prep_slave_single()

    PrepareDescriptor --> SetCallback: Set callback function
    SetCallback --> Submit: dmaengine_submit()
    Submit --> IssuePending: dma_async_issue_pending()
    IssuePending --> Running: DMA transfer active

    state Running {
        [*] --> Transferring
        Transferring --> WaitComplete: Hardware transfer
    }

    Running --> Callback: Transfer complete IRQ
    Callback --> UpdateTail: Update xmit tail pointer
    UpdateTail --> CheckWakeup: Check pending chars
    CheckWakeup --> Wakeup: chars < WAKEUP_CHARS
    Wakeup --> CheckMore: uart_write_wakeup()
    CheckWakeup --> CheckMore: chars >= WAKEUP_CHARS

    CheckMore --> PrepareTX: More data in buffer
    CheckMore --> RS485Stop: Buffer empty
    RS485Stop --> Idle: custom_uart_rs485_stop_tx()
```

---

## 9. RS-485 Timing Diagram

```
RS-485 Half-Duplex Timing:

                    delay_rts_before_send
                   |←───────────────────→|
    RTS (DE) ______|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|___________
                                                          ↑
                                                   delay_rts_after_send
                                                   |←────────────────→|

    TX Data  ______|       Data Transmission              |____________
                   ↑                                      ↑
             Start transmit                          Wait TEMT
             (after RTS delay)                       (TX empty)

    Bus State:
             IDLE → TRANSMIT → WAIT_EMPTY → POST_DELAY → IDLE
             (RX)   (TX mode)   (TX mode)    (TX mode)    (RX)

State Machine:
┌─────────┐    start_tx()    ┌───────────┐    data sent    ┌──────────┐
│  IDLE   │ ────────────────→│ TX_ACTIVE │ ──────────────→ │ TX_DONE  │
│(RX mode)│    Assert RTS    │(DE high)  │   Wait TEMT    │(Deassert)│
└─────────┘                  └───────────┘                 └──────────┘
     ↑                                                          │
     └──────────────────────────────────────────────────────────┘
                         After post-TX delay
```

---

## 10. Console Output Flow

```mermaid
flowchart TD
    subgraph Kernel["Kernel Console Output"]
        PRINTK[printk/pr_info]
        CONSOLE_WRITE[console->write]
    end

    subgraph Driver["Console Driver"]
        SAVE_IER[Save and disable IER]
        LOCK[spin_lock_irqsave]
        PUTCHAR[custom_uart_console_putchar]
        WAIT_THRE[Wait for THRE bit]
        WRITE_THR[Write to THR]
        WAIT_TEMT[Wait for TEMT]
        RESTORE_IER[Restore IER]
        UNLOCK[spin_unlock_irqrestore]
    end

    PRINTK --> CONSOLE_WRITE
    CONSOLE_WRITE --> LOCK
    LOCK --> SAVE_IER
    SAVE_IER --> PUTCHAR

    PUTCHAR --> WAIT_THRE
    WAIT_THRE --> WRITE_THR
    WRITE_THR --> NEXT{More chars?}
    NEXT -->|Yes| PUTCHAR
    NEXT -->|No| WAIT_TEMT

    WAIT_TEMT --> RESTORE_IER
    RESTORE_IER --> UNLOCK
    UNLOCK --> DONE([Return])
```

---

## 11. set_termios Flow

```mermaid
flowchart TD
    START([set_termios called]) --> GET_WORD[Get word length from CSIZE]

    GET_WORD --> CS5{CS5?}
    CS5 -->|Yes| LCR5[LCR = WLEN5]
    CS5 -->|No| CS6{CS6?}
    CS6 -->|Yes| LCR6[LCR = WLEN6]
    CS6 -->|No| CS7{CS7?}
    CS7 -->|Yes| LCR7[LCR = WLEN7]
    CS7 -->|No| LCR8[LCR = WLEN8]

    LCR5 --> CHECK_STOP
    LCR6 --> CHECK_STOP
    LCR7 --> CHECK_STOP
    LCR8 --> CHECK_STOP

    CHECK_STOP{CSTOPB set?}
    CHECK_STOP -->|Yes| STOP2[LCR |= STOP]
    CHECK_STOP -->|No| CHECK_PARITY

    STOP2 --> CHECK_PARITY

    CHECK_PARITY{PARENB set?}
    CHECK_PARITY -->|Yes| PARITY_EN[LCR |= PARITY]
    CHECK_PARITY -->|No| CALC_BAUD

    PARITY_EN --> CHECK_EVEN{PARODD clear?}
    CHECK_EVEN -->|Yes| EVEN_PAR[LCR |= EPAR]
    CHECK_EVEN -->|No| CALC_BAUD

    EVEN_PAR --> CALC_BAUD

    CALC_BAUD[Calculate baud rate divisor]
    CALC_BAUD --> LOCK[spin_lock_irqsave]

    LOCK --> UPDATE_TIMEOUT[uart_update_timeout]
    UPDATE_TIMEOUT --> SET_READ_MASK[Set read_status_mask]
    SET_READ_MASK --> SET_IGNORE_MASK[Set ignore_status_mask]
    SET_IGNORE_MASK --> CHECK_FLOW{CRTSCTS?}

    CHECK_FLOW -->|Yes| ENABLE_AFC[MCR |= AFE]
    CHECK_FLOW -->|No| DISABLE_AFC[MCR &= ~AFE]

    ENABLE_AFC --> WRITE_REGS
    DISABLE_AFC --> WRITE_REGS

    WRITE_REGS[Write LCR, set baud, write MCR]
    WRITE_REGS --> UNLOCK[spin_unlock_irqrestore]
    UNLOCK --> DONE([Return])
```

---

## 12. Baud Rate Calculation

```
Baud Rate Formula:

                        UART Clock
    Baud Rate = ─────────────────────────
                 16 × Divisor Latch

    Divisor = UART Clock / (16 × Desired Baud Rate)

    Divisor Latch = DLH:DLL (16-bit value)
    - DLL = Divisor & 0xFF      (low byte, offset 0x00 when DLAB=1)
    - DLH = (Divisor >> 8)      (high byte, offset 0x04 when DLAB=1)

Example (115200 baud with 48MHz clock):
    Divisor = 48,000,000 / (16 × 115200)
            = 48,000,000 / 1,843,200
            = 26 (0x001A)

    DLL = 0x1A
    DLH = 0x00

Programming Sequence:
┌────────────────────────────────────────────────┐
│ 1. Save current LCR value                      │
│ 2. Set DLAB bit (LCR[7] = 1)                   │
│ 3. Write DLL (divisor low byte)                │
│ 4. Write DLH (divisor high byte)               │
│ 5. Clear DLAB bit (restore LCR)                │
└────────────────────────────────────────────────┘
```

---

## 13. Module Initialization Flow

```mermaid
flowchart TD
    START([module_init]) --> LOG[pr_info: Driver init]
    LOG --> REG_UART[uart_register_driver]

    REG_UART --> CHECK_UART{Success?}
    CHECK_UART -->|No| FAIL1([Return error])
    CHECK_UART -->|Yes| REG_PLAT[platform_driver_register]

    REG_PLAT --> CHECK_PLAT{Success?}
    CHECK_PLAT -->|No| UNREG_UART[uart_unregister_driver]
    UNREG_UART --> FAIL2([Return error])
    CHECK_PLAT -->|Yes| SUCCESS([Return 0])

    subgraph ModuleExit["module_exit"]
        EXIT_START([module_exit]) --> UNREG_PLAT[platform_driver_unregister]
        UNREG_PLAT --> UNREG_UART2[uart_unregister_driver]
        UNREG_UART2 --> EXIT_DONE([Done])
    end
```

---

## 14. Device Tree Binding

```
Device Tree Example:

uart0: serial@10000000 {
    compatible = "vendor,custom-uart";
    reg = <0x10000000 0x1000>;
    interrupts = <GIC_SPI 10 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&uart_clk>;
    port-id = <0>;
    dmas = <&dma 0>, <&dma 1>;
    dma-names = "tx", "rx";
};

┌─────────────────────────────────────────────────────────┐
│                    Device Tree Node                     │
├─────────────────────────────────────────────────────────┤
│  compatible: "vendor,custom-uart"                       │
│                                                         │
│  ┌─────────────┬───────────────────────────────────┐    │
│  │  Property   │  Description                      │    │
│  ├─────────────┼───────────────────────────────────┤    │
│  │  reg        │  Register base and size           │    │
│  │  interrupts │  IRQ specification                │    │
│  │  clocks     │  Reference to UART clock          │    │
│  │  port-id    │  Port index (0 to UART_NR-1)      │    │
│  │  dmas       │  TX and RX DMA channel refs       │    │
│  │  dma-names  │  "tx", "rx"                       │    │
│  └─────────────┴───────────────────────────────────┘    │
│                                                         │
│  Kconfig Options:                                       │
│  - CONFIG_SERIAL_CUSTOM_UART         (tristate)         │
│  - CONFIG_SERIAL_CUSTOM_UART_CONSOLE (bool)             │
│  - CONFIG_SERIAL_CUSTOM_UART_DMA     (bool)             │
│  - CONFIG_SERIAL_CUSTOM_UART_RS485   (bool)             │
│  - CONFIG_SERIAL_CUSTOM_UART_NR_UARTS (int)             │
└─────────────────────────────────────────────────────────┘
```

---

## 15. Error Handling States

```mermaid
stateDiagram-v2
    [*] --> Normal: Operating

    Normal --> OverrunError: LSR[OE] set
    note right of OverrunError
        RX FIFO overflow
        Data lost
    end note
    OverrunError --> LogOverrun: icount.overrun++
    LogOverrun --> Normal: Continue

    Normal --> ParityError: LSR[PE] set
    note right of ParityError
        Parity bit mismatch
    end note
    ParityError --> LogParity: icount.parity++
    LogParity --> CheckIgnore: Check ignore mask
    CheckIgnore --> DropChar: Ignored
    CheckIgnore --> FlagParity: Not ignored
    FlagParity --> InsertChar: flag = TTY_PARITY
    DropChar --> Normal
    InsertChar --> Normal

    Normal --> FramingError: LSR[FE] set
    note right of FramingError
        Missing stop bit
    end note
    FramingError --> LogFrame: icount.frame++
    LogFrame --> Normal: Continue

    Normal --> BreakDetected: LSR[BI] set
    note right of BreakDetected
        Break condition on line
    end note
    BreakDetected --> LogBreak: icount.brk++
    LogBreak --> HandleBreak: uart_handle_break()
    HandleBreak --> CheckSysRq: Check for SysRq
    CheckSysRq --> SysRq: Handle magic
    CheckSysRq --> FlagBreak: flag = TTY_BREAK
    SysRq --> Normal
    FlagBreak --> Normal
```

---

## Summary

The Custom UART driver demonstrates:

1. **TTY/Serial Core Integration** - Standard Linux serial framework
2. **16550 Compatibility** - Industry-standard register interface
3. **Interrupt Handling** - TX/RX/Line Status/Modem interrupts
4. **DMA Support** - High-performance data transfer
5. **RS-485 Support** - Half-duplex differential signaling
6. **Console Support** - Early boot and kernel messages
7. **Flow Control** - Hardware (RTS/CTS) and software
8. **Power Management** - Suspend/resume support
9. **Clock Framework** - Dynamic clock management
10. **Device Tree** - Flexible hardware configuration
