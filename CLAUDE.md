# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

vibe is a RISC-V emulator written in C23 that implements the RV32I[MAC] instruction set. It emulates a complete RISC-V system including CPU, memory, and hardware peripherals.

## Build Commands

```bash
# Create build directory and configure with CMake
mkdir -p cmake-build-debug
cd cmake-build-debug
cmake ..

# Build the project
make -j8

# Run the emulator (from project root)
./cmake-build-debug/vibe                                  # Use default firmware/dtb/disk
./cmake-build-debug/vibe firmware.bin device.dtb disk.img # Specify custom files
./cmake-build-debug/vibe firmware.bin device.dtb disk.img 1000000 # Run for N instructions
```

## virtio Device Support

The emulator includes virtio-mmio devices for network and block storage:

- **virtio-net**: Provides network connectivity via TAP interface (Linux only)
  - Supports basic Ethernet frame transmission/reception
  - Enables SSH access for syzkaller fuzzing
  
- **virtio-blk**: Provides block storage access
  - Supports read/write operations on disk images
  - Compatible with ext2/3/4 filesystems

## Architecture

The emulator has a modular architecture with clear separation of concerns:

### Core Components

1. **CPU Core (`src/rv.c`, `src/rv.h`)**: Implements RISC-V instruction execution, registers, CSRs, and privilege levels. The CPU is designed to be independent of machine-specific details.

2. **Machine Layer (`src/mach.c`, `src/mach.h`)**: Integrates CPU with peripherals, manages memory map, and coordinates device updates. Acts as the system bus connecting all components.

3. **Main Loop (`src/main.c`)**: Handles firmware loading, ncurses UI, and runs the emulation loop with keyboard input support.

### Hardware Peripherals (`src/hw/`)

- **CLINT**: Core-Local Interruptor for timer and software interrupts
- **PLIC**: Platform-Level Interrupt Controller for external interrupts  
- **UART**: Two UART devices for serial communication with FIFO buffers
- **RTC**: Real-time clock for kernel timekeeping
- **virtio-net**: Network device for Ethernet communication (tap interface)
- **virtio-blk**: Block device for disk storage access
- **virtio-rng**: Random number generator for kernel entropy
- **NVMe**: NVMe 1.4 storage controller with admin and I/O queue support

### Memory Map

- `0x00101000`: RTC
- `0x02000000`: CLINT
- `0x03000000`: UART0
- `0x06000000`: UART1
- `0x0C000000`: PLIC
- `0x10001000`: virtio-net
- `0x10002000`: virtio-blk
- `0x10003000`: virtio-rng
- `0x80000000`: RAM base (128MB)
- `0x82000000`: Device Tree Blob location

## Key Design Patterns

- **Callback-based I/O**: Uses function pointers for bus operations and UART I/O
- **Device abstraction**: Each peripheral has init, bus handler, and update functions
- **Non-blocking terminal I/O**: Uses ncurses for interactive console emulation
- **Multi-entry TLB**: 32-entry fully-associative TLB with round-robin replacement for better performance

## Development Notes

- The project uses C23 standard with `-O3` optimization
- Code formatting follows LLVM style (see `.clang-format`)
- The `curses` library is required for the terminal UI
- Firmware images and device tree files are loaded from the `linux/` directory