# Arch-specific UAPI (`asm/`)

This directory provides **per-architecture** pieces of the Helios UAPI. These
headers capture details that vary by CPU/ABI and are selected by the toolchain
target + sysroot (no app changes required).

## What lives here
- Syscall numbers and calling-convention glue (`unistd.h`)
- Signal numbers and user signal context (`signal.h`)
- Memory/VM encodings and arch-only flags (`mman.h`, `param.h`)
- Thread/ptrace register layouts (`ptrace.h`, `ucontext.h`)
- Byte order helpers and low-level limits

## Relationship to `asm-generic/`
Prefer to start from a shared default and override the deltas:
```c
/* asm/feature.h */
#include <asm-generic/feature.h>  // defaults
/* …override only what differs for this arch… */
