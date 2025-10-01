# Helios UAPI (arch-independent)

This directory contains the **stable, public** headers that define Helios’s
userspace ABI (Application Binary Interface). Code here is *portable across
architectures* and should compile the same on any Helios target.

## What lives here

- Common error numbers, flags, and constants (`errno.h`, `fcntl.h`, `mman.h`, …)
- Public structs and types visible to apps (`stat.h`, `time.h`-like types, etc.)
- IOCTL numbers/layouts (arch-neutral portions)
- Device numbers and well-known interfaces

## Design rules

- **No kernel internals.** Do not include private kernel headers from here.
- **Fixed-width types only.** Prefer `<stdint.h>`; avoid surprising ABI changes.
- **Stable layout.** Public structs must be layout-stable; add fields only at
  the end with explicit padding if needed.
- **Pure C.** Keep headers C-only (no C++ features). Guard with `#ifdef __cplusplus`
  only if necessary for `extern "C"`.

## Arch specifics

Generic headers may `#include <asm/...>` for per-arch details *only when needed*.

## Typical usage

```c
#include <helios/errno.h>
