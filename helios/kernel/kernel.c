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
#include <drivers/fs/vfs.h>
#include <drivers/pci/pci.h>
#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/exec.h>
#include <kernel/helios.h>
#include <kernel/limine_requests.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/syscall.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <limine.h>
#include <mm/bootmem.h>
#include <mm/page.h>
#include <mm/page_alloc.h>
#include <mm/slab.h>
#include <util/log.h>

#include <stdlib.h>
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
	// FIXME: Once we get module loading working I will uncomment this
	// bootmem_reclaim_bootloader();

	liballoc_init(); // Just initializes the liballoc spinlock
	int* test = kmalloc(141);
	*test = 513;

	log_debug("kmalloc returned %p, stored %d in it", (void*)test, *test);

	scheduler_init();
	log_info("Initalizing syscalls");
	syscall_init();
	log_info("Initializing dmesg");
	dmesg_init();

	// GDB BREAKPOINT
	log_info("Initializing Timer");
	timer_init();

	// list_devices();
	// ctrl_init();
	//
	// sATADevice* fat_device = ctrl_get_device(3);
	// mount("/", fat_device, &fat_device->part_table[0], FAT16);

	log_info("Initializing VFS and mounting root ramfs");
	vfs_init();
	mount_initial_rootfs();

	vfs_mkdir("/test", VFS_PERM_ALL);
	vfs_mkdir("/subdir", VFS_PERM_ALL);
	vfs_mkdir("/test/test2", VFS_PERM_ALL);
	vfs_dump_child(vfs_lookup("/"));
	vfs_dump_child(vfs_lookup("/test/"));

	int fd = vfs_open("/test/testfile", O_CREAT | O_RDWR);
	if (fd < 0) {
		panic("Couldn't open file");
	}
	vfs_dump_child(vfs_lookup("/test/"));

	char buffer[] = "Hello how are you doing. Don't care fuck you";
	ssize_t w = vfs_write(fd, buffer, strlen(buffer));
	log_debug("Wrote %zd chars", w);

	vfs_lseek(fd, 0, SEEK_SET);

	char rbuf[sizeof(buffer)];
	ssize_t n = vfs_read(fd, rbuf, sizeof(rbuf) - 1);
	rbuf[n] = '\0';

	log_debug("Read %zd chars: '%s'", n, rbuf);

	vfs_close(fd);

#if 0

slab_test();
	struct limine_module_response* mod = mod_request.response;

	struct task* task = new_task("Hello world userspace", NULL);

	scheduler_dump();

	execve(task, mod->modules[0]->address);

	// We're done, just hang...
	log_warn("Shutting down in 3 seconds");
	sleep(1000);
	log_warn("Shutting down in 2 seconds");
	sleep(1000);
#endif
	log_warn("Shutting down in 1 second");
	sleep(1000);

	// QEMU shutdown command
	outword(0x604, 0x2000);
	for (;;)
		halt();
}
