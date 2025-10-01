/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct bootinfo_memmap_entry {
	uint64_t base;
	uint64_t length;
	uint64_t type;
};

struct bootinfo {
	bool valid; // Is this structure valid?

	struct bootinfo_memmap_entry* memmap;
	size_t memmap_entry_count;

	uint64_t hhdm_offset; // Offset for HHDM (High Half Direct Mapping)

	struct {
		uintptr_t physical_base; // Physical base address of the executable
		uintptr_t virtual_base; // Virtual base address of the executable
	} executable;
};

void bootinfo_init();
