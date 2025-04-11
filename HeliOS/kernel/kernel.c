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
#include <drivers/serial.h>
#include <kernel/memory/pmm.h>
#include <kernel/screen.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <limine.h>
#include <stdio.h>
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

__attribute__((used, section(".limine_requests"))) static volatile struct limine_framebuffer_request framebuffer_request
    = { .id = LIMINE_FRAMEBUFFER_REQUEST, .revision = 0 };

__attribute__((used, section(".limine_requests"))) static volatile struct limine_memmap_request memmap_request
    = { .id = LIMINE_MEMMAP_REQUEST, .revision = 0 };

__attribute__((used, section(".limine_requests"))) static volatile struct limine_hhdm_request hhdm_request
    = { .id = LIMINE_HHDM_REQUEST, .revision = 0 };

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used,
               section(".limine_requests_"
                       "start"))) static volatile LIMINE_REQUESTS_START_MARKER;

__attribute__((used, section(".limine_requests_end"))) static volatile LIMINE_REQUESTS_END_MARKER;

// Halt and catch fire function.
static void hcf(void)
{
    for (;;) {
#if defined(__x86_64__)
        asm("hlt");
#elif defined(__aarch64__) || defined(__riscv)
        asm("wfi");
#elif defined(__loongarch64)
        asm("idle 0");
#endif
    }
}

struct limine_framebuffer* framebuffer;

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

    // Fetch the first framebuffer.
    framebuffer = framebuffer_request.response->framebuffers[0];

    screen_init(framebuffer, COLOR_WHITE, COLOR_BLACK);
    init_serial();
    write_serial_string("\n\nInitialized serial output, expect a lot of debug messages :)\n\n");
    printf("Welcome to %s. Version: %s\n", KERNEL_NAME, KERNEL_VERSION);

    log_info("Initializing GDT");
    gdt_init();
    log_info("Initializing IDT");
    idt_init();
    log_info("Initializing Timer");
    timer_init();

    log_info("Initializing PMM");
    pmm_init(memmap_request.response, hhdm_request.response->offset);
    // TODO: allocate and deallocate physical pages

    // TODO: VMM initialization

    // puts("printf testing:");
    // printf("Hex: 0x%x 0x%X 0x%X\n", 0x14AF, 0x410BC, 0xABCDEF1221FEDCBA);
    // printf("pos dec: %d\n", 5611);
    // printf("neg dec: %d\n", -468);
    // printf("unsigned int: %d\n", 4184);

    // We're done, just hang...
    log_warn("entering infinite loop");
    hcf();
}
