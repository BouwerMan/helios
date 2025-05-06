/**
 * HeliOS is an open source hobby OS development project.
 * Copyright (C) 2024  Dylan Parks
 *
 * This program is free software: you can redistribute it and/or modify
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

// MASTER TODO
// TODO: clean up inports, such as size_t coming from <stddef>;
// TODO: Pretty sure PMM will perform weird things when reaching max memory
// TODO: Project restructuring (drivers, kernel, lib, etc.)
// TODO: Standardize return values
#include "../arch/x86_64/gdt.h"
#include <drivers/ata/controller.h>
#include <drivers/fs/vfs.h>
#include <drivers/pci/pci.h>
#include <drivers/serial.h>
#include <kernel/liballoc.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/screen.h>
#include <kernel/sys.h>
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <limine.h>
#include <string.h>
#include <util/log.h>

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

extern void liballoc_init();

void task_test()
{
	while (1) {
		log_debug("Task test");
		sleep(1000);
	}
}

void kernel_main(void)
{
	// TODO: Setup tty

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

	init_serial();
	write_serial_string("\n\nInitialized serial output, expect a lot of debug messages :)\n\n");
	screen_init(framebuffer, COLOR_WHITE, COLOR_BLACK);
	log_info("Welcome to %s. Version: %s", KERNEL_NAME, KERNEL_VERSION);

	log_info("Initializing GDT");
	gdt_init();
	log_info("Initializing IDT");
	idt_init();
	log_info("Initializing Timer");
	timer_init();

	liballoc_init(); // Just initializes the liballoc spinlock
	log_info("Initializing PMM");
	pmm_init(memmap_request.response, hhdm_request.response->offset);

	// TODO: VMM initialization
	log_info("Initializing VMM");
	vmm_init(memmap_request.response, exe_addr_req.response, hhdm_request.response->offset);

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
	vfs_close(&f);
	vfs_close(&f2);

	log_debug("Testing weird scheduler lists");
	init_scheduler();
	// struct task* next;
	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);
	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);
	// log_debug("Adding another task");
	struct task* task = task_add();
	task->entry = (void*)task_test;
	task->regs->rip = (uintptr_t)task_test;
	struct registers* registers = task->regs;
	log_error("Recieved interrupt #%lx with error code %lx on the default handler!", registers->int_no,
		  registers->err_code);
	log_error("RIP: %lx, RSP: %lx, RBP: %lx", registers->rip, registers->rsp, registers->rbp);
	log_error("RAX: %lx, RBX: %lx, RCX: %lx, RDX: %lx", registers->rax, registers->rbx, registers->rcx,
		  registers->rdx);
	log_error("RDI: %lx, RSI: %lx, RFLAGS: %lx, DS: %lx", registers->rdi, registers->rsi, registers->rflags,
		  registers->ds);
	log_error("CS: %lx, SS: %lx", registers->cs, registers->ss);
	log_error("R8: %lx, R9: %lx, R10: %lx, R11: %lx", registers->r8, registers->r9, registers->r10, registers->r11);
	log_error("R12: %lx, R13: %lx, R14: %lx, R15: %lx", registers->r12, registers->r13, registers->r14,
		  registers->r15);
	log_error("Saved_rflags: %lx", registers->saved_rflags);
	// log_debug("$d", 10 / 0);
	task->state = READY;

	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);
	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);
	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);
	// next = scheduler_pick_next();
	// log_debug("Found task with PID: %d", next->PID);

	// We're done, just hang...
	log_debug("Sleeping for 1 second");
	sleep(1000);
	log_warn("entering infinite loop");
	hcf();
}
