# HeliOS

A small, educational 64‑bit operating system for x86‑64. HeliOS boots via the Limine bootloader, runs as a freestanding C23 kernel, and includes a tiny libc and a couple of userspace samples to make the whole stack feel real.

> **Branches**
>
> * **main** – stable snapshots
> * **dev** – active development (latest features, occasionally sharp edges)

---

## Status & scope

HeliOS is a learning project. Expect incomplete features.

### Implemented

* Early console (serial + text framebuffer)
* GDT/IDT setup and basic interrupt/IRQ handling
* Physical memory: boot-time allocator → buddy allocator
* Kernel heap: slab allocator (with optional red‑zones/poisoning)
* Virtual memory manager (4‑level paging, higher‑half kernel)
* Simple scheduler and task/fork scaffolding
* ATA PIO storage driver
* VFS skeleton with RAMFS, early FAT, DEVFS, and TAR (initramfs) stubs
* Minimal libc and userspace samples (`/usr/bin/init.elf`, `hello_world.elf`)

### In progress / planned

* More complete filesystem support (FAT improvements, devfs polish)
* Syscalls & userspace runtime expansion
* SMP, ACPI, PCIe, and better device model

See `Documentation/` for subsystem overviews (init path, GDT, memory managers, etc.).

---

## Quick start

### Prereqs (host tools)

Linux host with: `git`, `curl`, `make`, `nasm`, `xorriso`, `qemu-system-x86_64`. A cross‑compiler will be built for you.

### 1) Build the cross‑compiler

```bash
./scripts/install_cross.sh
```

This downloads Binutils/GCC and installs them under `./tools`. The kernel uses C23 features; the script fetches a GCC version that supports them.

### 2) Build the OS image

```bash
make iso
```

This assembles `sysroot/`, packs an `initramfs.tar`, fetches Limine v9.x binaries (if needed), and produces `HeliOS.iso`.

### 3) Run in QEMU

```bash
make qemu
```

Default run uses 4 GiB RAM and boots the ISO with Limine. Check the top‑level `Makefile` for more targets (e.g., `qemugdb`, `clean`).

> Tip: `make qemugdb` starts QEMU paused and generates a `.gdbinit` via `scripts/gen_gdbinit.sh` for kernel debugging.

---

## Repository layout

```
Documentation/        High‑level docs (init sequence, GDT, memory subsystems)
helios/               Kernel sources
  arch/x86_64/        Architecture‑specific entry, GDT/IDT, paging, linker
  drivers/            Console/TTY/serial, ATA, PCI, fs drivers (devfs, fat, ramfs, tarfs)
  include/            Public kernel headers (types, mm, tasks, syscalls, etc.)
  kernel/             Core services (bootinfo, panic, syscalls, timer, workqueue)
  lib/                Kernel libc‑ish utilities (log, string, printf config)
  mm/                 Bootmem, buddy page allocator, slab, address spaces
libc/                 Freestanding libc for HeliOS (stdio/stdlib/string, crt)
userspace/            Sample programs (init, hello_world)
scripts/              Tooling (cross‑compiler install, gdbinit, FAT mounters)
```

---

## Build system

* **Cross toolchain**: Installed to `./tools/bin` via `scripts/install_cross.sh`. The top‑level `Makefile` checks for required binaries.
* **Sysroot**: Built in `./sysroot` and used when compiling libc and userspace.
* **Limine**: The `iso` target clones and builds Limine v9.x binaries under `./limine/` if missing.
* **Compiler flags**: Freestanding (`-ffreestanding`), no red‑zone, and `-std=gnu23`. See `helios/config.mk` and `helios/arch/*/config.mk`.

Common targets:

* `make headers` – install kernel/libc headers into the sysroot
* `make libc` / `make userspace` / `make helios` – build components
* `make initramfs` – pack `sysroot` as `boot/initramfs.tar`
* `make iso` – produce `HeliOS.iso`
* `make qemu` – run QEMU
* `make qemugdb` – run QEMU and wait for `gdb`
* `make clean` – remove build artifacts

---

## Coding style & tooling

* C23, tabs, 80‑column bias; see `.clang-format` at repo root
* Assembly uses NASM syntax; `.asm-lsp.toml` is provided for editor diagnostics
* Logging macros and panic paths prefer explicit, readable messages
* Debug‑friendly builds by default (`-Og -ggdb`)

---

## Contributing

Issues and PRs are welcome. Please keep changesets focused, include a brief rationale, and reference relevant docs in `Documentation/` when possible. For new subsystems, add a short design note.

---

## License

HeliOS is licensed under the [GNU General Public License v3.0](LICENSE). See the LICENSE file for more details.

---

## Acknowledgments

* Limine bootloader
* The broader OS‑dev community and literature that inspired many of these components
