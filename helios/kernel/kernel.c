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

#include <arch/mmu/vmm.h>
#include <drivers/ata/controller.h>
#include <drivers/console.h>
#include <drivers/fs/vfs.h>
#include <drivers/pci/pci.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <kernel/dmesg.h>
#include <kernel/exec.h>
#include <kernel/helios.h>
#include <kernel/irq_log.h>
#include <kernel/limine_requests.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/semaphores.h>
#include <kernel/syscall.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <kernel/work_queue.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <util/log.h>

#include <stdlib.h>
#define __STDC_WANT_LIB_EXT1__
#include <string.h>

struct limine_framebuffer* framebuffer;

struct kernel_context kernel = { 0 };

struct vfs_file* g_kernel_console = nullptr;

/**
 * init_kernel_structure - Initialize core kernel data structures
 */
void init_kernel_structure()
{
	list_init(&kernel.slab_caches);
}

/**
 * kernel_console_init - Initialize the kernel console for logging
 *
 * Sets up the kernel console by opening /dev/console and configuring
 * it as the primary output destination for kernel messages. This enables
 * kernel logging to be displayed on the system console.
 *
 * The function:
 * - Looks up the /dev/console device node in the VFS
 * - Allocates and initializes a file structure for the console
 * - Calls the TTY's open function to prepare the device
 * - Switches logging to buffered mode
 */
void kernel_console_init()
{
	struct vfs_dentry* dentry = vfs_lookup("/dev/console");
	if (dentry && dentry->inode) {
		log_debug("Found /dev/console, setting up kernel console");
		g_kernel_console = kzmalloc(sizeof(struct vfs_file));
		g_kernel_console->dentry = dget(dentry);
		g_kernel_console->fops = dentry->inode->fops;
		g_kernel_console->ref_count = 1;

		// Call the TTY's open function
		if (g_kernel_console->fops->open) {
			g_kernel_console->fops->open(dentry->inode,
						     g_kernel_console);
		}
		set_log_mode(LOG_BUFFERED);
	}
}

void kernel_main()
{
	extern void* g_entry_new_stack;
	log_debug("Successfully jumped to stack: %p", g_entry_new_stack);
	// FIXME: Once we get module loading working I will uncomment this
	// bootmem_reclaim_bootloader();

	liballoc_init(); // Just initializes the liballoc spinlock
	scheduler_init();
	syscall_init();
	work_queue_init();
	timer_init();

	// list_devices();
	// ctrl_init();
	//
	// sATADevice* fat_device = ctrl_get_device(3);
	// mount("/", fat_device, &fat_device->part_table[0], FAT16);

	log_info("Initializing VFS and mounting root ramfs");
	vfs_init();

	log_info("Mounting /dev");
	vfs_mkdir("/dev", VFS_PERM_ALL);
	vfs_mount(nullptr, "/dev", "devfs", 0);

	tty_init();

	irq_log_init();
	console_init();
	attach_tty_to_console("ttyS0");
	attach_tty_to_console("tty0");
	kernel_console_init();

	log_info("Successfully got out of bootstrapping hell");
	log_info("Welcome to %s. Version: %s", KERNEL_NAME, KERNEL_VERSION);

	int init_res = launch_init();
	if (init_res < 0) {
		log_error("Init error code: %d", init_res);
		panic("Could not launch init!");
	}

	scheduler_dump();

#if 0
	log_warn("Shutting down in 1 second");
	sleep(1000);

	// QEMU shutdown command
	console_flush();
	outword(0x604, 0x2000);
#endif
	log_info("Entering idle loop");
	for (;;) {
		halt();
	}
}
