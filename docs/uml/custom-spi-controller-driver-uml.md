# Custom SPI Controller Driver - UML Documentation

## Overview

The Custom SPI driver is a full-featured SPI master controller driver that integrates with the Linux SPI core framework. It demonstrates interrupt-driven and DMA transfers with GPIO-based chip select support.

**Driver Type:** Platform Device SPI Controller Driver
**File:** `drivers/peripheral/spi/custom_spi.c`
**Subsystems:** SPI Core, DMA Engine, GPIO, Clock, PM Runtime

---

## 1. Class Diagram (Data Structures)

```mermaid
classDiagram
    class custom_spi {
        +struct spi_controller *controller
        +struct device *dev
        +void __iomem *regs
        +struct clk *clk
        +int irq
        +const u8 *tx_buf
        +u8 *rx_buf
        +size_t tx_len
        +size_t rx_len
        +size_t count
        +unsigned int bits_per_word
        +struct completion done
        +int status
        +bool dma_enabled
        +struct custom_spi_dma tx_dma
        +struct custom_spi_dma rx_dma
        +dma_addr_t phys_addr
        +struct gpio_desc *cs_gpios[MAX_CS]
        +int num_cs
        +unsigned long transfers
        +unsigned long bytes_tx
        +unsigned long bytes_rx
        +unsigned long errors
    }

    class custom_spi_dma {
        +struct dma_chan *chan
        +struct dma_slave_config cfg
        +struct sg_table sgt
        +enum dma_data_direction dir
    }

    class spi_controller {
        +struct device dev
        +int bus_num
        +u16 num_chipselect
        +u32 mode_bits
        +u32 bits_per_word_mask
        +u32 min_speed_hz
        +u32 max_speed_hz
        +prepare_message()
        +transfer_one()
        +unprepare_message()
        +set_cs()
        +can_dma()
    }

    class spi_message {
        +struct list_head transfers
        +struct spi_device *spi
        +unsigned is_dma_mapped
        +complete()
        +context
        +status
    }

    class spi_transfer {
        +const void *tx_buf
        +void *rx_buf
        +unsigned len
        +dma_addr_t tx_dma
        +dma_addr_t rx_dma
        +unsigned speed_hz
        +u8 bits_per_word
    }

    class spi_device {
        +struct device dev
        +struct spi_controller *controller
        +u32 max_speed_hz
        +u8 chip_select
        +u8 bits_per_word
        +u32 mode
    }

    custom_spi --> spi_controller : manages
    custom_spi --> custom_spi_dma : tx_dma
    custom_spi --> custom_spi_dma : rx_dma
    spi_controller --> spi_message : processes
    spi_message --> spi_transfer : contains
    spi_message --> spi_device : for device
```

---

## 2. Component Diagram (System Architecture)

```mermaid
flowchart TB
    subgraph UserSpace["User Space"]
        SPIDEV["/dev/spidevX.Y"]
        APP[SPI Application]
    end

    subgraph KernelSpace["Kernel Space"]
        subgraph SPI["SPI Subsystem"]
            SPI_CORE[SPI Core]
            SPI_DEV[SPI Device Driver]
        end

        subgraph Driver["Custom SPI Driver"]
            CTRL_OPS[Controller Ops]
            PREPARE[prepare_message]
            TRANSFER[transfer_one]
            UNPREPARE[unprepare_message]
            PIO[PIO Transfer]
            DMA_XFER[DMA Transfer]
            IRQ_HANDLER[IRQ Handler]
        end

        subgraph DMA["DMA Engine"]
            DMA_ENGINE[DMA Controller]
            DMA_TX[TX Channel]
            DMA_RX[RX Channel]
        end

        subgraph GPIO["GPIO Subsystem"]
            GPIO_CTRL[GPIO Controller]
            CS_PINS[Chip Select GPIOs]
        end

        subgraph Clock["Clock Framework"]
            CLK[Clock Controller]
        end

        subgraph Platform["Platform Bus"]
            PLATFORM[Platform Driver]
            DT[Device Tree]
        end
    end

    subgraph Hardware["Hardware"]
        SPI_CTRL[SPI Controller]
        TX_FIFO[TX FIFO 64B]
        RX_FIFO[RX FIFO 64B]
        REGS[Register Bank]
        SCK[SCLK]
        MOSI[MOSI]
        MISO[MISO]
        CS[CS Lines]
    end

    APP --> SPIDEV
    SPIDEV --> SPI_DEV
    SPI_DEV --> SPI_CORE
    SPI_CORE --> CTRL_OPS
    CTRL_OPS --> PREPARE
    CTRL_OPS --> TRANSFER
    CTRL_OPS --> UNPREPARE
    TRANSFER --> PIO
    TRANSFER --> DMA_XFER
    DMA_XFER --> DMA_ENGINE
    DMA_ENGINE --> DMA_TX
    DMA_ENGINE --> DMA_RX
    IRQ_HANDLER --> PIO
    GPIO_CTRL --> CS_PINS
    PLATFORM --> CTRL_OPS
    DT --> PLATFORM
    CLK --> CTRL_OPS
    REGS --> TX_FIFO
    REGS --> RX_FIFO
    TX_FIFO --> MOSI
    RX_FIFO --> MISO
    SPI_CTRL --> SCK
    CS_PINS --> CS
```

---

## 3. Register Map Diagram

```
SPI Controller Register Map:

Offset  Name    R/W   Description
────────────────────────────────────────────────────────
0x00    CR0     R/W   Control Register 0
0x04    CR1     R/W   Control Register 1
0x08    DR      R/W   Data Register (FIFO access)
0x0C    SR      R     Status Register
0x10    CPSR    R/W   Clock Prescale Register
0x14    IMSC    R/W   Interrupt Mask Set/Clear
0x18    RIS     R     Raw Interrupt Status
0x1C    MIS     R     Masked Interrupt Status
0x20    ICR     W     Interrupt Clear Register
0x24    DMACR   R/W   DMA Control Register

┌─────────────────────────────────────────────────────────┐
│                    CR0 (0x00)                           │
├────────────────────────────────────────────────────────┤
│ [15:8]   SCR    - Serial Clock Rate (0-255)            │
│ [7]      SPH    - Clock Phase (0=first edge, 1=second) │
│ [6]      SPO    - Clock Polarity (0=low, 1=high idle)  │
│ [5:4]    FRF    - Frame Format (00=SPI, 01=TI, 10=MW)  │
│ [3:0]    DSS    - Data Size Select (bits-1, 0011-1111) │
└────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                    CR1 (0x04)                           │
├────────────────────────────────────────────────────────┤
│ [3]      SOD    - Slave Output Disable                 │
│ [2]      MS     - Master/Slave (0=Master, 1=Slave)     │
│ [1]      SSE    - SPI Enable (1=Enabled)               │
│ [0]      LBM    - Loopback Mode                        │
└────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                    SR (0x0C)                            │
├─────┬─────┬─────┬─────┬─────────────────────────────────┤
│  4  │  3  │  2  │  1  │  0                              │
├─────┼─────┼─────┼─────┼─────────────────────────────────┤
│ BSY │ RFF │ RNE │ TNF │ TFE                             │
│Busy │RX   │RX   │TX   │TX FIFO                          │
│     │Full │Not  │Not  │Empty                            │
│     │     │Empty│Full │                                 │
└─────┴─────┴─────┴─────┴─────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│                    IMSC/RIS/MIS/ICR                     │
├─────┬─────┬─────┬─────┬─────────────────────────────────┤
│  3  │  2  │  1  │  0  │                                 │
├─────┼─────┼─────┼─────┼─────────────────────────────────┤
│ TX  │ RX  │ RT  │ ROR │                                 │
│FIFO │FIFO │RX   │RX   │                                 │
│Half │Half │Time │Over │                                 │
│Empty│Full │out  │run  │                                 │
└─────┴─────┴─────┴─────┴─────────────────────────────────┘
```

---

## 4. SPI Mode Configuration

```
SPI Modes (CPOL/CPHA Configuration):

Mode 0 (CPOL=0, CPHA=0):
        ┌───┐   ┌───┐   ┌───┐   ┌───┐
SCLK ───┘   └───┘   └───┘   └───┘   └───
        ↑       ↑       ↑       ↑
    Sample   Sample  Sample  Sample

Mode 1 (CPOL=0, CPHA=1):
        ┌───┐   ┌───┐   ┌───┐   ┌───┐
SCLK ───┘   └───┘   └───┘   └───┘   └───
            ↑       ↑       ↑       ↑
         Sample  Sample  Sample  Sample

Mode 2 (CPOL=1, CPHA=0):
    ────┐   ┌───┐   ┌───┐   ┌───┐   ┌───
SCLK    └───┘   └───┘   └───┘   └───┘
        ↑       ↑       ↑       ↑
    Sample   Sample  Sample  Sample

Mode 3 (CPOL=1, CPHA=1):
    ────┐   ┌───┐   ┌───┐   ┌───┐   ┌───
SCLK    └───┘   └───┘   └───┘   └───┘
            ↑       ↑       ↑       ↑
         Sample  Sample  Sample  Sample

┌─────────────────────────────────────────────────────┐
│  Mode  │  CPOL  │  CPHA  │  CR0[SPO]  │  CR0[SPH]  │
├────────┼────────┼────────┼────────────┼────────────┤
│   0    │   0    │   0    │     0      │     0      │
│   1    │   0    │   1    │     0      │     1      │
│   2    │   1    │   0    │     1      │     0      │
│   3    │   1    │   1    │     1      │     1      │
└────────┴────────┴────────┴────────────┴────────────┘
```

---

## 5. Transfer State Machine

```mermaid
stateDiagram-v2
    [*] --> Idle: Driver Ready

    Idle --> PrepareMessage: spi_sync/async()
    PrepareMessage --> SetMode: custom_spi_prepare_message()
    SetMode --> AssertCS: Set CPOL/CPHA

    state TransferLoop {
        [*] --> ConfigureTransfer
        ConfigureTransfer --> SetSpeed: Set clock rate
        SetSpeed --> SetBPW: Set bits per word
        SetBPW --> FlushFIFO: Clear FIFOs

        FlushFIFO --> EnableSPI: custom_spi_enable()
        EnableSPI --> CheckDMA: Can use DMA?

        CheckDMA --> DMATransfer: len >= DMA_MIN_BYTES
        CheckDMA --> PIOTransfer: len < DMA_MIN_BYTES

        DMATransfer --> WaitDMAComplete
        WaitDMAComplete --> TransferDone: DMA callback

        PIOTransfer --> EnableInterrupts
        EnableInterrupts --> WaitPIOComplete
        WaitPIOComplete --> TransferDone: completion

        TransferDone --> DisableSPI
        DisableSPI --> WaitIdle: custom_spi_wait_idle()
        WaitIdle --> NextTransfer: More transfers?
        NextTransfer --> ConfigureTransfer: Yes
        NextTransfer --> [*]: No
    }

    AssertCS --> TransferLoop
    TransferLoop --> DeassertCS: custom_spi_unprepare_message()
    DeassertCS --> Idle: Complete
```

---

## 6. Sequence Diagram (Full Transfer)

```mermaid
sequenceDiagram
    participant SPI_Core as SPI Core
    participant Driver as SPI Driver
    participant DMA as DMA Engine
    participant HW as SPI Hardware
    participant Device as SPI Device

    SPI_Core->>Driver: custom_spi_prepare_message()
    Driver->>Driver: custom_spi_set_mode()
    Driver->>Driver: custom_spi_cs_control(enable)
    Driver-->>SPI_Core: return 0

    loop For each transfer
        SPI_Core->>Driver: custom_spi_transfer_one()
        Driver->>Driver: custom_spi_set_speed()
        Driver->>Driver: custom_spi_set_bits_per_word()
        Driver->>Driver: custom_spi_flush_fifo()
        Driver->>HW: custom_spi_enable()

        alt DMA Transfer (len >= 64)
            Driver->>Driver: custom_spi_can_dma()
            Driver->>DMA: dmaengine_prep_slave_single(TX)
            DMA-->>Driver: TX descriptor
            Driver->>DMA: dmaengine_prep_slave_single(RX)
            DMA-->>Driver: RX descriptor
            Driver->>HW: Enable DMA (DMACR)
            Driver->>DMA: dmaengine_submit(RX)
            Driver->>DMA: dmaengine_submit(TX)
            Driver->>DMA: dma_async_issue_pending()

            DMA->>HW: Transfer data
            HW-->>DMA: RX complete
            DMA->>Driver: custom_spi_dma_rx_callback()
            Driver->>Driver: complete(&done)

        else PIO Transfer
            Driver->>HW: Enable interrupts (IMSC)
            Driver->>Driver: custom_spi_pio_write()
            Driver->>HW: Write TX data to DR

            loop Until complete
                HW->>Driver: IRQ (TX/RX)
                Driver->>Driver: custom_spi_pio_read()
                Driver->>Driver: custom_spi_pio_write()
            end

            Driver->>Driver: complete(&done)
        end

        Driver->>Driver: wait_for_completion_timeout()
        Driver->>HW: custom_spi_wait_idle()
        Driver->>HW: custom_spi_disable()
        Driver-->>SPI_Core: return status
    end

    SPI_Core->>Driver: custom_spi_unprepare_message()
    Driver->>Driver: custom_spi_cs_control(disable)
    Driver-->>SPI_Core: return 0
```

---

## 7. IRQ Handler State Diagram

```mermaid
stateDiagram-v2
    [*] --> ReadMIS: IRQ Triggered

    ReadMIS --> NoInterrupt: MIS == 0
    NoInterrupt --> [*]: IRQ_NONE

    ReadMIS --> ClearInterrupts: MIS != 0
    ClearInterrupts --> CheckROR: Write ICR

    CheckROR --> HandleOverrun: ROR bit set
    HandleOverrun --> LogError: Increment errors
    LogError --> SetError: status = -EIO
    SetError --> Complete1: complete(&done)
    Complete1 --> [*]: IRQ_HANDLED

    CheckROR --> CheckRX: ROR not set

    CheckRX --> HandleRX: RX or RT bit set
    HandleRX --> ReadFIFO: custom_spi_pio_read()
    ReadFIFO --> CheckTX

    CheckRX --> CheckTX: No RX interrupt

    CheckTX --> HandleTX: TX bit set
    HandleTX --> WriteFIFO: custom_spi_pio_write()
    WriteFIFO --> CheckComplete

    CheckTX --> CheckComplete: No TX interrupt

    CheckComplete --> TransferDone: tx_len==0 && rx_len==0
    TransferDone --> DisableInt: Disable interrupts
    DisableInt --> SetSuccess: status = 0
    SetSuccess --> Complete2: complete(&done)
    Complete2 --> [*]: IRQ_HANDLED

    CheckComplete --> [*]: IRQ_HANDLED (continue)
```

---

## 8. Clock Configuration

```
SPI Clock Calculation:

                      SPI_CLK
    SCLK = ─────────────────────────
            CPSR × (1 + SCR)

    Where:
    - SPI_CLK = Input clock frequency
    - CPSR = Clock Prescale (2-254, even only)
    - SCR = Serial Clock Rate (0-255)

    Minimum SCLK = SPI_CLK / (254 × 256)
    Maximum SCLK = SPI_CLK / 2

Algorithm to find CPSR and SCR:
┌──────────────────────────────────────────────────────┐
│  for cpsr = 2; cpsr <= 254; cpsr += 2:               │
│      scr = (SPI_CLK / (cpsr × target_speed)) - 1     │
│      if scr <= 255:                                  │
│          break  // Found valid combination           │
│                                                      │
│  Actual speed = SPI_CLK / (cpsr × (1 + scr))         │
└──────────────────────────────────────────────────────┘

Example (10 MHz target with 100 MHz clock):
    CPSR = 2, SCR = 4
    SCLK = 100MHz / (2 × 5) = 10 MHz
```

---

## 9. PIO Transfer Flow

```mermaid
flowchart TD
    subgraph Write["PIO Write"]
        W_START([Start Write]) --> W_CHECK{tx_len > 0?}
        W_CHECK -->|No| W_DONE([Done])
        W_CHECK -->|Yes| W_FIFO{TNF set?}
        W_FIFO -->|No| W_DONE
        W_FIFO -->|Yes| W_DATA{tx_buf valid?}
        W_DATA -->|Yes 8-bit| W_READ8[Read *tx_buf++]
        W_DATA -->|Yes 16-bit| W_READ16[Read *(u16*)tx_buf]
        W_DATA -->|No| W_ZERO[val = 0]
        W_READ8 --> W_WRITE
        W_READ16 --> W_WRITE
        W_ZERO --> W_WRITE
        W_WRITE[Write DR] --> W_DEC[tx_len--, count++]
        W_DEC --> W_CHECK
    end

    subgraph Read["PIO Read"]
        R_START([Start Read]) --> R_CHECK{rx_len > 0?}
        R_CHECK -->|No| R_DONE([Done])
        R_CHECK -->|Yes| R_FIFO{RNE set?}
        R_FIFO -->|No| R_DONE
        R_FIFO -->|Yes| R_READ[Read DR]
        R_READ --> R_BUF{rx_buf valid?}
        R_BUF -->|Yes 8-bit| R_STORE8[*rx_buf++ = val]
        R_BUF -->|Yes 16-bit| R_STORE16[*(u16*)rx_buf = val]
        R_BUF -->|No| R_DISCARD[Discard]
        R_STORE8 --> R_DEC
        R_STORE16 --> R_DEC
        R_DISCARD --> R_DEC
        R_DEC[rx_len--] --> R_CHECK
    end
```

---

## 10. DMA Transfer Flow

```mermaid
flowchart TD
    START([Start DMA Transfer]) --> INIT[Initialize count, reinit completion]

    INIT --> PREP_TX{TX buffer?}
    PREP_TX -->|Yes| TX_DESC[dmaengine_prep_slave_single TX]
    TX_DESC --> TX_CB[Set TX callback]
    PREP_TX -->|No| PREP_RX

    TX_CB --> PREP_RX{RX buffer?}
    PREP_RX -->|Yes| RX_DESC[dmaengine_prep_slave_single RX]
    RX_DESC --> RX_CB[Set RX callback]
    PREP_RX -->|No| ENABLE_DMA

    RX_CB --> ENABLE_DMA[Enable DMACR bits]

    ENABLE_DMA --> SUBMIT_RX{RX desc?}
    SUBMIT_RX -->|Yes| RX_SUBMIT[dmaengine_submit RX]
    RX_SUBMIT --> RX_ISSUE[dma_async_issue_pending RX]
    SUBMIT_RX -->|No| SUBMIT_TX

    RX_ISSUE --> SUBMIT_TX{TX desc?}
    SUBMIT_TX -->|Yes| TX_SUBMIT[dmaengine_submit TX]
    TX_SUBMIT --> TX_ISSUE[dma_async_issue_pending TX]
    SUBMIT_TX -->|No| WAIT

    TX_ISSUE --> WAIT[wait_for_completion_timeout]

    WAIT --> TIMEOUT{Timeout?}
    TIMEOUT -->|Yes| ERROR[status = -ETIMEDOUT]
    TIMEOUT -->|No| SUCCESS[status = 0]

    ERROR --> DISABLE[Disable DMACR]
    SUCCESS --> DISABLE

    DISABLE --> TERMINATE{Error?}
    TERMINATE -->|Yes| TERM_DMA[dmaengine_terminate_sync]
    TERMINATE -->|No| DONE

    TERM_DMA --> DONE([Return status])
```

---

## 11. Chip Select Control

```mermaid
classDiagram
    class ChipSelectControl {
        +cs_gpios[MAX_CS]: gpio_desc*
        +num_cs: int
        +control(spi_dev, enable)
    }

    class GPIOChipSelect {
        +gpiod_set_value_cansleep()
        +Active Low (default)
        +Active High (SPI_CS_HIGH)
    }

    class HardwareChipSelect {
        +SPI controller managed
        +Automatic assertion
    }

    ChipSelectControl --> GPIOChipSelect : uses
    ChipSelectControl --> HardwareChipSelect : alternative

    note for ChipSelectControl "Driver uses GPIO descriptors\nfor flexible CS control"
```

```
Chip Select Timing:

CS (Active Low):
    ──────┐                                      ┌──────
          └──────────────────────────────────────┘
          ↑                                      ↑
    Assert CS                              Deassert CS
    (prepare_message)                    (unprepare_message)

           ┌─────────────────────────────────┐
    SCLK ──┤  Clock during transfer          ├─────
           └─────────────────────────────────┘

           ┌─────────────────────────────────┐
    MOSI ──┤  TX Data                        ├─────
           └─────────────────────────────────┘

           ┌─────────────────────────────────┐
    MISO ──┤  RX Data                        ├─────
           └─────────────────────────────────┘

GPIO Control Logic:
┌───────────────────────────────────────────────────────┐
│  if (mode & SPI_CS_HIGH)                              │
│      gpiod_set_value(cs, enable ? 1 : 0);  // Active high │
│  else                                                 │
│      gpiod_set_value(cs, enable ? 0 : 1);  // Active low  │
└───────────────────────────────────────────────────────┘
```

---

## 12. Activity Diagram (Probe Function)

```mermaid
flowchart TD
    START([Start Probe]) --> ALLOC[devm_spi_alloc_host]
    ALLOC --> GET_DATA[spi_controller_get_devdata]
    GET_DATA --> INIT_COMP[init_completion]

    INIT_COMP --> GET_MEM[platform_get_resource MEM]
    GET_MEM --> IOREMAP[devm_ioremap_resource]
    IOREMAP --> CHECK_MAP{Mapping OK?}
    CHECK_MAP -->|No| ERROR1[Return error]
    CHECK_MAP -->|Yes| GET_CLK[devm_clk_get]

    GET_CLK --> CHECK_CLK{Clock OK?}
    CHECK_CLK -->|No| ERROR2[Return error]
    CHECK_CLK -->|Yes| GET_IRQ[platform_get_irq]

    GET_IRQ --> CHECK_IRQ{IRQ valid?}
    CHECK_IRQ -->|No| ERROR3[Return error]
    CHECK_IRQ -->|Yes| GET_CS[Get CS GPIOs]

    GET_CS --> LOOP_CS[Loop: devm_gpiod_get_index_optional]
    LOOP_CS --> ENABLE_CLK[clk_prepare_enable]

    ENABLE_CLK --> HW_INIT[custom_spi_hw_init]
    HW_INIT --> REQ_IRQ[devm_request_irq]

    REQ_IRQ --> CHECK_IRQREQ{IRQ request OK?}
    CHECK_IRQREQ -->|No| DISABLE_CLK[clk_disable_unprepare]
    DISABLE_CLK --> ERROR4[Return error]
    CHECK_IRQREQ -->|Yes| DMA_INIT[custom_spi_dma_init]

    DMA_INIT --> CONFIG_CTRL[Configure spi_controller]

    CONFIG_CTRL --> SET_BUS[bus_num = pdev->id]
    SET_BUS --> SET_CS[num_chipselect = num_cs]
    SET_CS --> SET_MODE[mode_bits = CPOL|CPHA|CS_HIGH|LSB]
    SET_MODE --> SET_BPW[bits_per_word_mask = 4-16]
    SET_BPW --> SET_SPEED[min/max_speed_hz]
    SET_SPEED --> SET_OPS[Set operation callbacks]

    SET_OPS --> REGISTER[devm_spi_register_controller]
    REGISTER --> CHECK_REG{Register OK?}
    CHECK_REG -->|No| DMA_RELEASE[custom_spi_dma_release]
    DMA_RELEASE --> DISABLE_CLK2[clk_disable_unprepare]
    DISABLE_CLK2 --> ERROR5[Return error]
    CHECK_REG -->|Yes| LOG[dev_info: Controller registered]

    LOG --> SUCCESS([Return 0])

    ERROR1 --> FAIL([Return error])
    ERROR2 --> FAIL
    ERROR3 --> FAIL
    ERROR4 --> FAIL
    ERROR5 --> FAIL
```

---

## 13. Power Management States

```mermaid
stateDiagram-v2
    [*] --> Probed: probe() complete

    Probed --> Active: Operating
    note right of Active
        - Clock enabled
        - SPI transfers possible
    end note

    Active --> Suspended: custom_spi_suspend()
    note right of Suspended
        1. spi_controller_suspend()
        2. custom_spi_disable()
        3. clk_disable_unprepare()
    end note

    Suspended --> Active: custom_spi_resume()
    note left of Active
        1. clk_prepare_enable()
        2. custom_spi_hw_init()
        3. spi_controller_resume()
    end note

    Active --> Removed: custom_spi_remove()
    note right of Removed
        1. custom_spi_disable()
        2. custom_spi_dma_release()
        3. clk_disable_unprepare()
        4. Log statistics
    end note

    Removed --> [*]
```

---

## 14. Device Tree Binding

```
Device Tree Example:

spi0: spi@20000000 {
    compatible = "vendor,custom-spi";
    reg = <0x20000000 0x1000>;
    interrupts = <GIC_SPI 20 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&spi_clk>;
    cs-gpios = <&gpio 8 GPIO_ACTIVE_LOW>,
               <&gpio 7 GPIO_ACTIVE_LOW>;
    dmas = <&dma 2>, <&dma 3>;
    dma-names = "tx", "rx";
    #address-cells = <1>;
    #size-cells = <0>;

    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <40000000>;
    };

    sensor@1 {
        compatible = "vendor,sensor";
        reg = <1>;
        spi-max-frequency = <1000000>;
        spi-cpol;
        spi-cpha;
    };
};

┌─────────────────────────────────────────────────────────┐
│                    Device Tree Node                     │
├─────────────────────────────────────────────────────────┤
│  compatible: "vendor,custom-spi"                        │
│                                                         │
│  ┌─────────────┬───────────────────────────────────┐    │
│  │  Property   │  Description                      │    │
│  ├─────────────┼───────────────────────────────────┤    │
│  │  reg        │  Register base and size           │    │
│  │  interrupts │  IRQ specification                │    │
│  │  clocks     │  Reference to SPI clock           │    │
│  │  cs-gpios   │  Chip select GPIO list            │    │
│  │  dmas       │  TX and RX DMA channel refs       │    │
│  │  dma-names  │  "tx", "rx"                       │    │
│  └─────────────┴───────────────────────────────────┘    │
│                                                         │
│  Kconfig Options:                                       │
│  - CONFIG_SPI_CUSTOM              (tristate)            │
│  - CONFIG_SPI_CUSTOM_DMA          (bool)                │
│  - CONFIG_SPI_CUSTOM_DMA_MIN_BYTES (int, default 64)    │
│  - CONFIG_SPI_CUSTOM_DEBUG        (bool)                │
│  - CONFIG_SPI_CUSTOM_MAX_CHIPSELECT (int, default 4)    │
└─────────────────────────────────────────────────────────┘
```

---

## 15. Data Flow Summary

```mermaid
flowchart LR
    subgraph TX_Path["TX Path"]
        TX_APP[User TX Data]
        TX_BUF[TX Buffer]
        TX_FIFO[TX FIFO]
        TX_SHIFT[TX Shift Register]
        MOSI_LINE[MOSI]
    end

    subgraph RX_Path["RX Path"]
        MISO_LINE[MISO]
        RX_SHIFT[RX Shift Register]
        RX_FIFO[RX FIFO]
        RX_BUF[RX Buffer]
        RX_APP[User RX Data]
    end

    subgraph Clock_Path["Clock Generation"]
        CLK_SRC[Clock Source]
        PRESCALER[CPSR Prescaler]
        SCR_DIV[SCR Divider]
        SCLK[SCLK Output]
    end

    TX_APP --> TX_BUF
    TX_BUF -->|DMA/PIO| TX_FIFO
    TX_FIFO --> TX_SHIFT
    TX_SHIFT --> MOSI_LINE

    MISO_LINE --> RX_SHIFT
    RX_SHIFT --> RX_FIFO
    RX_FIFO -->|DMA/PIO| RX_BUF
    RX_BUF --> RX_APP

    CLK_SRC --> PRESCALER
    PRESCALER --> SCR_DIV
    SCR_DIV --> SCLK
```

---

## Summary

The Custom SPI driver demonstrates:

1. **SPI Core Integration** - Standard Linux SPI framework
2. **Controller Operations** - prepare/transfer/unprepare callbacks
3. **PIO Transfers** - Interrupt-driven FIFO management
4. **DMA Transfers** - High-performance scatter-gather
5. **GPIO Chip Select** - Flexible multi-device support
6. **Clock Management** - Dynamic frequency configuration
7. **SPI Modes** - All CPOL/CPHA combinations
8. **Power Management** - Suspend/resume support
9. **Device Tree** - Flexible hardware configuration
10. **Error Handling** - Overrun detection and recovery
