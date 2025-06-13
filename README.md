# HELIOS

HeliOS is a small hobby operating system built for fun and learning. It boots via the Limine bootloader and targets 64‑bit x86 systems. The project is still young but already sports a few neat tricks.

## What it can do

* Basic kernel console output
* GDT/IDT setup with interrupt handling
* Physical and virtual memory managers (buddy + slab)
* Simple task scheduler
* ATA PIO storage driver
* Early FAT support through a rudimentary VFS

## Building the toolchain

A custom cross‑compiler is required. You can build it automatically by running:

```bash
./scripts/install_cross.sh
```

This downloads Binutils and GCC and installs them into `./tools`. Make sure you have the usual build tools (gcc, make, curl, etc.) available.

## Building and running HeliOS

Once the toolchain is installed, building an ISO image is as simple as:

```bash
make iso
```

You can boot the resulting image with:

```bash
make qemu
```

Feel free to peek at the Makefile for more targets.

## Contributing

Pull requests are very welcome! If you spot a bug or have an improvement in mind, open an issue or PR and let's chat.

## License

HeliOS is licensed under the [GNU General Public License v3.0](LICENSE). See the LICENSE file for more details.
