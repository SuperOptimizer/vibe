# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

vibe is a RISC-V emulator written in C23 that implements the RV32I[MAC] instruction set. It emulates a complete RISC-V system including CPU, memory, and hardware peripherals.

## Build Commands

### Building the Emulator

```bash
# Create build directory and configure with CMake
mkdir -p cmake-build-debug
cd cmake-build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug  # Options: Debug, Release, RelWithDebInfo, MinSizeRel

# Build the project
make -j8

# Build with sanitizers (for debugging)
make vibe-san

# Run the emulator (from project root)
./cmake-build-debug/vibe                                  # Use default firmware/dtb/disk
./cmake-build-debug/vibe firmware.bin device.dtb disk.img # Specify custom files
./cmake-build-debug/vibe firmware.bin device.dtb disk.img 1000000 # Run for N instructions
```

### Building Linux Images with Buildroot

```bash
# Build Linux kernel, device tree, and root filesystem
cd linux
./do_build.sh

# This will create:
# - vmlinux: Linux kernel binary
# - vibe.dtb: Device tree blob
# - rootfs.ext2: Root filesystem image
```

## virtio Device Support

The emulator includes virtio-mmio devices for network, block storage, and random number generation:

- **virtio-net**: Provides network connectivity via TAP interface (Linux only)
  - Supports basic Ethernet frame transmission/reception
  - Enables SSH access for syzkaller fuzzing
  
- **virtio-blk**: Provides block storage access
  - Supports read/write operations on disk images
  - Compatible with ext2/3/4 filesystems
  - Root device is typically `/dev/vda`

- **virtio-rng**: Provides random number generation
  - Supplies entropy to the guest kernel
  - Essential for cryptographic operations

## Architecture

The emulator has a modular architecture with clear separation of concerns:

### Core Components

1. **CPU Core (`src/rv.c`, `src/rv.h`)**: Implements RISC-V instruction execution, registers, CSRs, and privilege levels. The CPU is designed to be independent of machine-specific details.

2. **Machine Layer (`src/mach.c`, `src/mach.h`)**: Integrates CPU with peripherals, manages memory map, and coordinates device updates. Acts as the system bus connecting all components.

3. **Main Loop (`src/main.c`)**: Handles firmware loading, ncurses UI, and runs the emulation loop with keyboard input support.

### Hardware Peripherals (`src/hw/`)

- **CLINT**: Core-Local Interruptor for timer and software interrupts
- **PLIC**: Platform-Level Interrupt Controller for external interrupts (32 devices)
- **UART**: Two UART devices for serial communication with FIFO buffers
- **RTC**: Real-time clock for kernel timekeeping
- **virtio-net**: Network device for Ethernet communication (tap interface)
- **virtio-blk**: Block device for disk storage access
- **virtio-rng**: Random number generator for kernel entropy

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

- The project uses C23 standard with various optimization levels
- Release builds use `-O3 -march=native -flto` for maximum performance
- Debug builds include full debugging symbols
- Sanitizer builds (`vibe-san`) enable AddressSanitizer and UndefinedBehaviorSanitizer
- Code formatting follows LLVM style (see `.clang-format`)
- Required libraries: `curses`, `tinfo`, `dl`
- Firmware images and device tree files are loaded from the `linux/` directory
- Buildroot 2023.11.1 is used for creating Linux root filesystems