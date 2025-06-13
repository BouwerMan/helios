<!-- markdownlint-configure-file { "MD013": { "line_length": 120} } -->
# Kernel Initialization Sequence

This document outlines the kernel initialization procedure as defined in `__arch_entry()`, the architecture-specific entry point for all supported platforms.

The function is designed to serve as the *first C-level function executed* after bootloader handoff. It performs validation of bootloader-provided data, sets up low-level system facilities, and prepares for full kernel execution. The initialization process is split into distinct phases for clarity, modularity, and future extensibility.

---

## Overview

The init routine is responsible for:

- Validating Limine boot protocol compatibility
- Initializing early output mechanisms (serial, framebuffer)
- Installing kernel-controlled GDT and IDT tables
- Bringing up physical memory management (boot allocator → buddy allocator)
- Setting up the kernel’s virtual memory manager and page tables
- Allocating and switching to a kernel-managed stack
- Transitioning to `kernel_main()`

> **Note:** `__arch_entry()` is defined per architecture (e.g., `x86_64/__arch_entry.c`, `aarch64/__arch_entry.c`) to allow platform-specific setup while keeping the core sequence consistent.

---

## Initialization Phases

### 🔒 0. Sanity Checks

- Confirms that the bootloader supports the requested Limine base revision.
- Validates the presence of at least one framebuffer.
- If these checks fail, the system halts to avoid undefined behavior.

### 📤 1. Early Output Initialization

- Serial output is initialized first for reliable debug logging, even in headless systems.
- The framebuffer is initialized to allow visual output; default text colors are set.
- A debug banner or message can be printed to confirm early I/O is working.

### 🧠 2. Descriptor Tables

- **GDT (Global Descriptor Table):**  
  A clean GDT is loaded to ensure correct segment descriptors, support for TSS, and 64-bit mode stability.
- **IDT (Interrupt Descriptor Table):**  
  The IDT is explicitly loaded, as its state is undefined upon bootloader handoff.

### 🧱 3. Physical Memory Initialization

- A temporary boot memory allocator is initialized using Limine's memory map.
- Memory region metadata (usable, reclaimable, reserved, etc.) is parsed and stored.
- Basic system structures relying on memory allocation (e.g., bootinfo) are initialized.

### 🧮 4. Full Physical Memory Manager

- A buddy allocator is set up to replace the boot allocator.
- Once initialized, non-critical boot memory (including the boot allocator itself) is released back into the system.
- Bootloader-reclaimable memory is evaluated and reclaimed where safe.

### 🧭 5. Virtual Memory Setup

- The kernel sets up its own paging structures (e.g., new PML4).
- A new CR3 value is loaded, detaching from the bootloader’s page table hierarchy.
- Higher Half Direct Mapping (HHDM) and framebuffer regions are remapped with correct caching flags.

### 🪜 6. Kernel Stack and Transition

- A fresh kernel stack is allocated using the new buddy allocator.
- The stack is page-aligned and optionally protected (e.g., red zone or guard page).
- A trampoline is used to atomically switch stacks and jump to `kernel_main()`.
- At this point, the bootloader is fully out of the picture.

---

## Design Goals

- **Architecture Independence:**  
  The structure and responsibilities of `__arch_entry()` are consistent across architectures, with platform-specific implementations delegated as needed.

- **Reclaiming Control:**  
  Transitioning to kernel-owned resources (GDT, IDT, CR3, stack) as early as possible ensures deterministic behavior and full system control.

- **Safe Bootloader Detachment:**  
  Bootloader-reclaimable memory is freed only after the kernel stack and page tables are no longer reliant on it.

---

## Future Considerations

- Add detailed logging of each initialization phase (timings, memory usage, etc.)
- Integrate symbol resolution and backtrace support early in the stack transition.
- Add early-stage fault handlers or fallback debugging shell via serial console.
- Support modular init callbacks (for optional subsystems like ACPI, SMP, etc.)

---

## Entry Function Declaration

```c
/**
 * @brief Architecture-specific kernel entry point.
 *
 * This function is called after bootloader handoff and performs
 * platform-specific setup and generic kernel initialization. Once
 * complete, it transitions to `kernel_main()` with a fresh stack.
 *
 * This function does not return.
 */
void __arch_entry(void);
