/**
 * @file arch/x86_64/entry.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <arch/gdt/gdt.h>
#include <arch/idt.h>
#include <arch/mmu/vmm.h>
#include <drivers/serial.h>
#include <kernel/bootinfo.h>
#include <kernel/helios.h>
#include <kernel/limine_requests.h>
#include <kernel/screen.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page_alloc.h>
#include <util/log.h>

[[noreturn]]
extern void __switch_to_new_stack(void* new_stack_top,
				  void (*entrypoint)(void));

void* g_entry_new_stack = nullptr;

/**
 * @brief Architecture-specific kernel entry point.
 *
 * This function is called after bootloader handoff and performs
 * platform-specific setup and generic kernel initialization. Once
 * complete, it transitions to `kernel_main()` with a fresh stack.
 *
 * This function does not return.
 */
[[gnu::used]]
void __arch_entry()
{
	// Stage 0: Sanity checks

	// Ensure the bootloader actually understands our base revision (see spec).
	if (LIMINE_BASE_REVISION_SUPPORTED == false) {
		for (;;)
			halt();
	}

	init_kernel_structure();

	// Stage 1: Initialize logging and framebuffer

	serial_port_init();
	screen_init(COLOR_WHITE, COLOR_BLACK);

	// Stage 2: Initialize descriptor tables

	log_init("Init Stage 2: Initializing descriptor tables");

	log_debug("Initializing GDT");
	gdt_init();
	log_debug("Initializing IDT");
	idt_init();

	// Stage 3: Initializing boot time memory management

	log_init("Init Stage 3: Initializing boot time memory management");

	bootmem_init();

	bootinfo_init();

	// Stage 4: Fully initialize memory management

	log_init("Init Stage 4: Fully initializing memory management");

	page_alloc_init();

	// Stage 5: Initialize virtual memory management

	log_init("Init Stage 5: Initializing virtual memory management");
	vmm_init();

	log_info(TESTING_HEADER, "VMM Pruning");
	vmm_test_prune_single_mapping();
	log_info(TESTING_FOOTER, "VMM Pruning");

	// Stage 6: Initialize kernel stack and jump to kernel_main

	log_init(
		"Init Stage 6: Initializing kernel stack and jumping to kernel_main");

	// Bottom of stack
	void* new_stack = get_free_pages(AF_KERNEL, STACK_SIZE_PAGES);
	g_entry_new_stack =
		(void*)((uptr)new_stack + STACK_SIZE_PAGES * PAGE_SIZE);
	__switch_to_new_stack(g_entry_new_stack, kernel_main);
	__builtin_unreachable();
}
