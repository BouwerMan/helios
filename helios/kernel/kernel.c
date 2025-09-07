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

#include "drivers/console.h"
#include "drivers/kbd.h"
#include "drivers/tty.h"
#include "fs/ustar/tar.h"
#include "fs/vfs.h"
#include "kernel/helios.h"
#include "kernel/irq_log.h"
#include "kernel/limine_requests.h"
#include "kernel/panic.h"
#include "kernel/syscall.h"
#include "kernel/tasks/scheduler.h"
#include "kernel/timer.h"
#include "kernel/work_queue.h"
#include "lib/log.h"
#include "limine.h"
#include "mm/kmalloc.h"

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
	struct vfs_dentry* dentry = vfs_lookup("/dev/ttyS0");
	if (dentry && dentry->inode) {
		log_debug("Found /dev/console, setting up kernel console");
		g_kernel_console = kzalloc(sizeof(struct vfs_file));
		g_kernel_console->dentry = dget(dentry);
		g_kernel_console->fops = dentry->inode->fops;
		g_kernel_console->ref_count++;

		// Call the TTY's open function
		if (g_kernel_console->fops->open) {
			g_kernel_console->fops->open(dentry->inode,
						     g_kernel_console);
		}
		set_log_mode(LOG_BUFFERED);
	} else {
		log_error("Could not find /dev/ttyS0 for kernel console!");
	}
}

void kernel_main()
{
	extern void* g_entry_new_stack;
	log_debug("Successfully jumped to stack: %p", g_entry_new_stack);
	// FIXME: Once we get module loading working I will uncomment this
	// bootmem_reclaim_bootloader();

	liballoc_init(); // Just initializes the liballoc spinlock

	log_info("Initializing VFS and mounting root ramfs");
	vfs_init();

	scheduler_init();
	syscall_init();
	work_queue_init();
	timer_init();

	// list_devices();
	// ctrl_init();
	//
	// sATADevice* fat_device = ctrl_get_device(3);
	// mount("/", fat_device, &fat_device->part_table[0], FAT16);

	test_split_path();

	log_info("Mounting initial root filesystem");
	unpack_tarfs(mod_request.response->modules[0]->address);

	int fd = vfs_open("/", O_RDONLY);
	struct vfs_file* f = get_file(fd);
	// vfs_dump_child(f->dentry);

	// goto loop;

	log_info("Mounting /dev");
	vfs_mkdir("/dev", VFS_PERM_ALL);
	int res = vfs_mount(nullptr, "/dev", "devfs", 0);
	if (res < 0) {
		log_error("Could not mount /dev: %d", res);
		panic("Could not mount /dev");
	}

	log_info("Opening directory for reading");
	int fd2 = vfs_open("/usr/bin/", O_RDONLY);
	struct vfs_file* f2 = get_file(fd2);
	struct dirent* dirent = kzalloc(sizeof(struct dirent));
	off_t offset = 0;
	while (vfs_readdir(f2, dirent, offset++)) {
		log_debug(
			"Found entry: %s, d_ino: %lu, d_off: %lu, d_reclen: %d, d_type: %d",
			dirent->d_name,
			dirent->d_ino,
			dirent->d_off,
			dirent->d_reclen,
			dirent->d_type);
	}

	vfs_normalize_path("./usr/testdir/../dir2", get_file(fd2)->dentry);

	vfs_close(fd);
	vfs_close(fd2);

	// ramfs_test();

	tty_init();

	irq_log_init();
	console_init();
	attach_tty_to_console("ttyS0");
	attach_tty_to_console("tty0");
	keyboard_init();
	kernel_console_init();

	log_info("Successfully got out of bootstrapping hell");
	log_info("Welcome to %s. Version: %s", KERNEL_NAME, KERNEL_VERSION);

	log_info("Sleeping for %d", 1000);
	sleep(1000);
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
loop:
	log_info("Entering idle loop");
	for (;;) {
		halt();
	}
}
