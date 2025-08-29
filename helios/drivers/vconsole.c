/**
 * @file drivers/vconsole.c
 *
 * Copyright (C) 2025  Dylan Parks
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

#include <drivers/vconsole.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/types.h>
#include <mm/kmalloc.h>
#include <mm/page.h>
#include <mm/page_alloc.h>

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct tty_driver vconsole_driver = {
	.write = vconsole_tty_write,
};

static constexpr size_t RING_BUFFER_SIZE_PAGES = 8;
static constexpr size_t RING_BUFFER_SIZE = RING_BUFFER_SIZE_PAGES * PAGE_SIZE;

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * vconsole_tty_init - Initialize the VGA console TTY device
 *
 * Creates and registers a TTY device named "tty0" that outputs to the VGA
 * text console. Allocates memory for the output ring buffer and initializes
 * all necessary data structures. This TTY serves as the primary console
 * output device for the system.
 *
 * Panics if memory allocation for the ring buffer fails, as the console
 * TTY is essential for system operation.
 */
void vconsole_tty_init()
{
	struct tty* tty = kzalloc(sizeof(struct tty));
	tty->driver = &vconsole_driver;
	strncpy(tty->name, "tty0", 32);

	struct ring_buffer* rb = &tty->output_buffer;
	rb->buffer = get_free_pages(AF_KERNEL, RING_BUFFER_SIZE_PAGES);
	if (!rb->buffer) {
		panic("Didn't get free pages");
	}
	rb->size = RING_BUFFER_SIZE;
	spinlock_init(&rb->lock);

	sem_init(&tty->write_lock, 1);
	register_tty(tty);
}

/**
 * vconsole_tty_write - Drain the TTY output buffer to the VGA console
 * @tty: Pointer to the TTY device whose output buffer to drain
 *
 * Reads all available characters from the TTY's output ring buffer and
 * displays them on the VGA text console. This function is typically called
 * as a work item to process buffered output. The operation is atomic and
 * protected by the ring buffer's spinlock to ensure thread safety.
 *
 * Return: Number of characters written to the console
 */
ssize_t vconsole_tty_write(struct tty* tty)
{
	struct ring_buffer* rb = &tty->output_buffer;
	ssize_t bytes_written = 0;

	sem_wait(&tty->write_lock);

	while (rb->head != rb->tail) {
		screen_putchar(rb->buffer[rb->tail]);
		rb->tail = (rb->tail + 1) % rb->size;
		bytes_written++;
	}

	sem_signal(&tty->write_lock);

	return bytes_written;
}
