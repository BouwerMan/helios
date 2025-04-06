
# HELIOS

HeliOS is a hobby operating system project aimed at learning and exploring low-level systems programming. The project is currently in its early stages, but it already includes several key features and components.

## Features

- [X] Kernel printing
- [X] Global Descriptor Table (GDT)
- [X] Interrupt Descriptor Table (IDT)
- [X] Interrupt handling
- [X] Physical Page Manager
- [X] Virtual Page Manager
- [X] Paging with kernel loaded at `0xC0000000`
- [X] ATA PIO Driver
- [ ] FAT Driver (Work in progress)

## Getting Started

### Prerequisites

To build and run HeliOS, you will need:

- A GCC cross-compiler targeting `i686-elf`. Follow [this tutorial](https://wiki.osdev.org/GCC_Cross-Compiler) to set it up.
- QEMU or another x86 emulator for testing.

## License

HeliOS is licensed under the [GNU General Public License v3.0](LICENSE). See the LICENSE file for more details.
