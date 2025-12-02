# Linux Device Driver UML Documentation

This directory contains comprehensive UML diagrams and documentation for all drivers in this tutorial repository. Each document includes class diagrams, sequence diagrams, state machines, activity diagrams, and component diagrams.

## Driver Documentation Index

### 1. Sensor Drivers

| Driver | Type | Subsystems | Documentation |
|--------|------|------------|---------------|
| **MPU6050** | I2C/IIO | I2C, IIO, Regmap, Triggered Buffer | [mpu6050-imu-driver-uml.md](mpu6050-imu-driver-uml.md) |
| **HC-SR04** | GPIO/IIO | GPIO, IIO, IRQ, ktime | [hc-sr04-ultrasonic-driver-uml.md](hc-sr04-ultrasonic-driver-uml.md) |

### 2. GPS/GNSS Drivers

| Driver | Type | Subsystems | Documentation |
|--------|------|------------|---------------|
| **NEO-M8N** | Serial/GNSS | GNSS, serdev, Regulator, GPIO | [neo-m8n-gps-driver-uml.md](neo-m8n-gps-driver-uml.md) |

### 3. Peripheral Controller Drivers

| Driver | Type | Subsystems | Documentation |
|--------|------|------------|---------------|
| **Custom UART** | TTY/Serial | TTY, Serial Core, DMA, Clock | [custom-uart-controller-driver-uml.md](custom-uart-controller-driver-uml.md) |
| **Custom SPI** | SPI Controller | SPI Core, DMA, GPIO, Clock | [custom-spi-controller-driver-uml.md](custom-spi-controller-driver-uml.md) |

---

## UML Diagram Types

Each driver documentation includes:

### Class Diagrams
- Data structure relationships
- Kernel structure dependencies
- Driver-specific structures

### Component Diagrams
- System architecture overview
- Subsystem integration
- Hardware connections

### Sequence Diagrams
- Data flow operations
- Initialization sequences
- Interrupt handling

### State Machine Diagrams
- Driver operational states
- Protocol state machines
- Power management states

### Activity Diagrams
- Probe function flow
- Transfer operations
- Error handling paths

---

## Diagram Rendering

These diagrams use [Mermaid](https://mermaid.js.org/) markdown syntax for portability. To view the rendered diagrams:

1. **GitHub** - Renders Mermaid diagrams automatically
2. **VS Code** - Use Mermaid Preview extension
3. **Online** - Use [Mermaid Live Editor](https://mermaid.live/)
4. **Documentation tools** - MkDocs, Docusaurus, GitBook support Mermaid

---

## Quick Reference

### Linux Kernel Subsystems Used

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Application Layer                            │
├─────────────────┬─────────────┬─────────────┬───────────────────────┤
│   /dev/ttyX     │ /dev/spidevX│ /dev/iio:X  │     /dev/gnssX        │
├─────────────────┼─────────────┼─────────────┼───────────────────────┤
│   TTY/Serial    │  SPI Core   │  IIO Core   │     GNSS Core         │
├─────────────────┼─────────────┼─────────────┼───────────────────────┤
│  UART Driver    │ SPI Driver  │ MPU6050     │     NEO-M8N           │
│  (custom_uart)  │(custom_spi) │ HC-SR04     │    (ublox_neo)        │
├─────────────────┴─────────────┴─────────────┴───────────────────────┤
│              Platform Bus / I2C Bus / Serial Device Bus             │
├─────────────────────────────────────────────────────────────────────┤
│                         Hardware Layer                              │
│    UART Controller | SPI Controller | I2C + GPIO | UART Interface   │
└─────────────────────────────────────────────────────────────────────┘
```

### Common Design Patterns

| Pattern | Drivers Using It | Description |
|---------|------------------|-------------|
| **Platform Driver** | UART, SPI, HC-SR04 | Platform bus device model |
| **I2C Client** | MPU6050 | I2C slave device driver |
| **Serdev Client** | NEO-M8N | Serial device driver |
| **IIO Subsystem** | MPU6050, HC-SR04 | Industrial I/O framework |
| **Regmap** | MPU6050 | Register map abstraction |
| **DMA Engine** | UART, SPI | DMA transfer support |
| **Triggered Buffer** | MPU6050 | IIO buffered capture |
| **Completion** | All | Synchronization primitive |

---

## Learning Path

Recommended order for studying the drivers:

1. **HC-SR04** - Simplest GPIO-based IIO driver
2. **MPU6050** - I2C with IIO triggered buffer
3. **NEO-M8N** - Serial device with protocol parsing
4. **Custom SPI** - SPI controller with DMA
5. **Custom UART** - Full UART with TTY integration

Each level builds on concepts from the previous drivers.

---

## Contributing

When adding new drivers to this repository:

1. Create a new `<driver>-uml.md` file in this directory
2. Include all standard UML diagram types
3. Update this README index
4. Follow the existing documentation format
