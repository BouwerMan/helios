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
#include <drivers/tty.h>
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
extern void __switch_to_new_stack(void* new_stack_top,
				  void (*entrypoint)(void));

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

	int fd = vfs_open("/testfile", O_CREAT | O_RDWR);
	if (fd < 0) {
		panic("Couldn't open file");
	}

	char buffer[] = "Hello how are you doing. Don't care fuck you";
	ssize_t w = vfs_write(fd, buffer, strlen(buffer));
	log_debug("Wrote %zd chars", w);

	vfs_lseek(fd, 0, SEEK_SET);

	char rbuf[sizeof(buffer)];
	ssize_t n = vfs_read(fd, rbuf, sizeof(rbuf) - 1);
	rbuf[n] = '\0';

	log_debug("Read %zd chars: '%s'", n, rbuf);

	vfs_close(fd);

	test_tokenizer();

	vfs_mkdir("/dev", VFS_PERM_ALL);
	vfs_mkdir("/test", VFS_PERM_ALL);
	vfs_mkdir("/test/testdir", VFS_PERM_ALL);
	vfs_mkdir("/test/testdir/testdir2", VFS_PERM_ALL);
	vfs_dump_child(vfs_lookup("/"));
	vfs_dump_child(vfs_lookup("/test"));
	vfs_dump_child(vfs_lookup("/test/testdir"));

	log_info("Mounting /dev");
	vfs_mount(nullptr, "/dev", "devfs", 0);

	tty_init();
	int stdout = vfs_open("/dev/stdout", O_RDWR);
	if (stdout < 0) {
		log_error("Failed to get stdout: %s",
			  vfs_get_err_name(-stdout));
		goto end_stdout; // jump past our other testing
	}

	log_debug("Got stdout fd: %d", stdout);

	vfs_write(stdout,
		  "Hello from the kernel! This is a test of the TTY driver.\n",
		  58);

end_stdout:

#if 0

	slab_test();
	struct limine_module_response* mod = mod_request.response;

	struct task* task = new_task("Hello world userspace", NULL);

	scheduler_dump();

	execve(task, mod->modules[0]->address);

	// We're done, just hang...
#endif
	log_warn("Shutting down in 1 second");
	sleep(1000);

	// QEMU shutdown command
	outword(0x604, 0x2000);
	for (;;)
		halt();
}
