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
#include "drivers/term.h"
#include "drivers/tty.h"
#include "fs/ustar/tar.h"
#include "fs/vfs.h"
#include "kernel/helios.h"
#include "kernel/irq_log.h"
#include "kernel/klog.h"
#include "kernel/limine_requests.h"
#include "kernel/panic.h"
#include "kernel/softirq.h"
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

extern void* g_entry_new_stack;

/**
 * init_kernel_structure - Initialize core kernel data structures
 */
void init_kernel_structure()
{
	list_init(&kernel.slab_caches);
}

void kernel_main()
{
	log_debug("Successfully jumped to stack: %p", g_entry_new_stack);
	// FIXME: Once we get module loading working I will uncomment this
	// bootmem_reclaim_bootloader();

	liballoc_init(); // Just initializes the liballoc spinlock

	log_info("Initializing VFS and mounting root ramfs");
	vfs_init();

	scheduler_init();
	softirq_init();
	syscall_init();
	work_queue_init();
	timer_init();
	term_init();

	log_init("Initializing klog");
	struct klog_ring* ring = klog_init();
	kernel.klog = ring;

	set_log_mode(LOG_KLOG);

	// list_devices();
	// ctrl_init();
	//
	// sATADevice* fat_device = ctrl_get_device(3);
	// mount("/", fat_device, &fat_device->part_table[0], FAT16);

	test_split_path();

	log_info("Mounting initial root filesystem");
	unpack_tarfs(mod_request.response->modules[0]->address);

	int fd = vfs_open("/", O_RDONLY);

	log_info("Mounting /dev");
	vfs_mkdir("/dev", VFS_PERM_ALL);
	int res = vfs_mount(nullptr, "/dev", "devfs", 0);
	if (res < 0) {
		log_error("Could not mount /dev: %d", res);
		panic("Could not mount /dev");
	}

	log_info("Opening directory for reading");
	int fd2 = vfs_open("/usr/", O_RDONLY);
	struct vfs_file* f2 = get_file(fd2);
	struct dirent* dirent = kzalloc(sizeof(struct dirent));
	while (vfs_readdir(f2, dirent, DIRENT_GET_NEXT)) {
		log_debug(
			"Found entry: %s, d_ino: %lu, d_off: %lu, d_reclen: %d, d_type: %d",
			dirent->d_name,
			dirent->d_ino,
			dirent->d_off,
			dirent->d_reclen,
			dirent->d_type);
	}

	vfs_close(fd);
	vfs_close(fd2);

	// ramfs_test();

	tty_init();

	irq_log_init();
	console_init();
	// attach_tty_to_console("ttyS0");
	attach_tty_to_console("tty0");
	keyboard_init();

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
	yield_blocked();
	log_info("Entering idle loop");
	for (;;) {
		yield();
	}
}
