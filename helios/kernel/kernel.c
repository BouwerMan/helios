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

[[noreturn]]
extern void __switch_to_new_stack(void* new_stack_top, void (*entrypoint)(void));

struct limine_framebuffer* framebuffer;

struct kernel_context kernel = { 0 };

/// Initializes the lists in the kernel_context struct
/// A lot of the entries actually get inited by other functions
void init_kernel_structure()
{
	list_init(&kernel.slab_caches);
}

void kernel_main()
{
	log_info("Successfully got out of bootstrapping hell");
	log_info("Welcome to %s. Version: %s", KERNEL_NAME, KERNEL_VERSION);
	bootmem_reclaim_bootloader();

	liballoc_init(); // Just initializes the liballoc spinlock
	int* test = kmalloc(141);
	*test = 513;

	log_debug("kmalloc returned %p, stored %d in it", (void*)test, *test);

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
	for (;;)
		halt();
}
