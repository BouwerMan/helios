/**
 * HeliOS is an open source hobby OS development project.
 * Copyright (C) 2024-2025  Dylan Parks
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

#include <arch/x86_64/gdt/gdt.h>
#include <drivers/ata/controller.h>
#include <drivers/fs/vfs.h>
#include <drivers/pci/pci.h>
#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/helios.h>
#include <kernel/liballoc.h>
#include <kernel/mmu/vmm.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <util/log.h>

#define __STDC_WANT_LIB_EXT1__
#include <string.h>

// Set the base revision to 3, this is recommended as this is the latest
// base revision described by the Limine boot protocol specification.
// See specification for further info.

__attribute__((used, section(".limine_requests"))) static volatile LIMINE_BASE_REVISION(3);

// The Limine requests can be placed anywhere, but it is important that
// the compiler does not optimise them away, so, usually, they should
// be made volatile or equivalent, _and_ they should be accessed at least
// once or marked as used with the "used" attribute as done here.

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request
	framebuffer_request = { .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0 };

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request = {
	.id = LIMINE_MEMMAP_REQUEST,
	.revision = 0
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request = {
	.id = LIMINE_HHDM_REQUEST,
	.revision = 0
};

__attribute__((used, section(".limine_requests"))) static volatile struct limine_executable_address_request
	exe_addr_req = { .id = LIMINE_EXECUTABLE_ADDRESS_REQUEST, .revision = 0 };

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void)
{
	for (;;) {
#if defined(__x86_64__)
		__asm__("hlt");
#elif defined(__aarch64__) || defined(__riscv)
		asm("wfi");
#elif defined(__loongarch64)
		asm("idle 0");
#endif
	}
}

struct limine_framebuffer* framebuffer;

struct kernel_context kernel = { 0 };

/// Initializes the lists in the kernel_context struct
static void init_kernel_structure()
{
	list_init(&kernel.slab_caches);
	kernel.memmap = memmap_request.response;
}

// Doing some quirky stuff to get around clang and gcc errors for functions called from asm
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-prototypes"
void kernel_main(void)
{
#pragma GCC diagnostic pop
	// Ensure the bootloader actually understands our base revision (see spec).
	if (LIMINE_BASE_REVISION_SUPPORTED == false) {
		hcf();
	}

	// Ensure we got a framebuffer.
	if (framebuffer_request.response == NULL || framebuffer_request.response->framebuffer_count < 1) {
		hcf();
	}

	// Ensure we got a memory map
	if (memmap_request.response == NULL || memmap_request.response->entry_count < 1) {
		hcf();
	}

	// Ensure we got a hhdm
	if (hhdm_request.response == NULL) {
		hcf();
	}

	// Ensure we get an executable address
	if (exe_addr_req.response == NULL) {
		hcf();
	}

	// Fetch the first framebuffer.
	framebuffer = framebuffer_request.response->framebuffers[0];

	init_kernel_structure();

	init_serial();
	write_serial_string("\n\nInitialized serial output, expect a lot of debug messages :)\n\n");
	screen_init(framebuffer, COLOR_WHITE, COLOR_BLACK);
	log_info("Welcome to %s. Version: %s", KERNEL_NAME, KERNEL_VERSION);

	log_info("Initializing GDT");
	gdt_init();
	log_info("Initializing IDT");
	idt_init();

	log_info("Initializing memory management");
	bootmem_init(kernel.memmap);

	page_alloc_init();

	liballoc_init(); // Just initializes the liballoc spinlock
	int* test = kmalloc(141);
	*test = 513;

	log_debug("kmalloc returned %p, stored %d in it", (void*)test, *test);

	log_info("Initializing VMM");
	vmm_init();
	log_debug("VMM initialized, cr3: %lx", vmm_read_cr3());

	init_scheduler();
	log_info("Initializing dmesg");
	dmesg_init();

	log_info("Initializing Timer");
	timer_init();

#if 1
	list_devices();
	ctrl_init();
	vfs_init(64);

	sATADevice* fat_device = ctrl_get_device(3);
	mount("/", fat_device, &fat_device->part_table[0], FAT16);

	struct vfs_file f = { 0 };
	int res2 = vfs_open("/dir/test2.txt", &f);
	if (res2 < 0) {
		log_error("oh no");
	} else {
		// log_info("%s", f.read_ptr);
	}
	log_info("open 2");
	struct vfs_file f2 = { 0 };
	res2 = vfs_open("/test2.txt", &f2);
	if (res2 < 0) {
		log_error("oh no");
	} else {
		log_info("f_size: %zu, at %lx", f2.file_size, (uint64_t)f2.read_ptr);
		// log_debug_long(f2.read_ptr);
	}
	log_debug("Closing");
	vfs_close(&f);
	vfs_close(&f2);
#endif

	log_info(TESTING_HEADER, "Slab Allocator");

	struct slab_cache test_cache = { 0 };
	(void)slab_cache_init(&test_cache, "Test cache", sizeof(uint64_t), 0, NULL, NULL);
	log_debug("Test cache slab size: %d pages", SLAB_SIZE_PAGES);

	test_use_before_alloc(&test_cache);
	test_buffer_overflow(&test_cache);
	test_buffer_underflow(&test_cache);
	test_valid_usage(&test_cache);
	test_object_alignment(&test_cache);

	slab_cache_purge_corrupt(&test_cache);

	uint64_t* data = slab_alloc(&test_cache);
	*data = 12345;
	log_info("Got data at %p, set value to %lu", (void*)data, *data);
	uint64_t* data2 = slab_alloc(&test_cache);
	*data2 = 54321;
	log_info("Got data2 at %p, set value to %lu", (void*)data2, *data2);
	size_t slab_bytes = SLAB_SIZE_PAGES * PAGE_SIZE;
	size_t mask = ~(slab_bytes - 1);
	log_debug("Slab base for data: %lx", (uintptr_t)data & mask);
	slab_dump_stats(&test_cache);
	slab_free(&test_cache, data2);

	slab_cache_destroy(&test_cache);
	(void)slab_alloc(&test_cache);
	slab_dump_stats(&test_cache);

	log_info(TESTING_FOOTER, "Slab Allocator");

	// We're done, just hang...
	log_warn("Shutting down in 3 seconds");
	sleep(1000);
	log_warn("Shutting down in 2 seconds");
	sleep(1000);
	log_warn("Shutting down in 1 second");
	sleep(1000);
	// QEMU shutdown command
	outword(0x604, 0x2000);
	hcf();
}
