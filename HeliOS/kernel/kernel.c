/**
 *   HeliOS is an open source hobby OS development project.
 *   Copyright (C) 2024  Dylan Parks
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// MASTER TODO
// TODO: clean up inports, such as size_t coming from <stddef>;
// TODO: Pretty sure PMM will perform weird things when reaching max memory
// TODO: Project restructuring (drivers, kernel, lib, etc.)
// TODO: Standardize return values
#include "../arch/x86_64/gdt.h"
#include <drivers/serial.h>
#include <kernel/screen.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <limine.h>
#include <stdio.h>
#include <string.h>

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

// Finally, define the start and end markers for the Limine requests.
// These can also be moved anywhere, to any .c file, as seen fit.

__attribute__((used, section(".limine_requests_start"))) static volatile LIMINE_REQUESTS_START_MARKER;

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

    // Fetch the first framebuffer.
    framebuffer = framebuffer_request.response->framebuffers[0];

    // Note: we assume the framebuffer model is RGB with 32-bit pixels.
    // for (size_t i = 0; i < 100; i++) {
    //     volatile uint32_t* fb_ptr = framebuffer->address;
    //     fb_ptr[i * (framebuffer->pitch / 4) + i] = 0xffffff;
    // }

    screen_init(framebuffer, COLOR_WHITE, COLOR_BLACK);
    init_serial();
    write_serial_string("\n\nInitialized serial output, expect a lot of debug messages :)\n\n");
    printf("Welcome to %s. Version: %s\n", KERNEL_NAME, KERNEL_VERSION);

    puts("Initializing GDT");
    gdt_init();
    puts("Initialized GDT, surely it won't break :)");

    puts("Initializing IDT");
    idt_init();
    puts("Initializing Timer");
    timer_init();

    sleep(10000);
    puts("10 seconds or so passed");

    // We're done, just hang...
    puts("entering infinite loop");
    hcf();
}
