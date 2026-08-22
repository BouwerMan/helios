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

#include "drivers/vconsole.h"
#include "drivers/screen.h"
#include "drivers/term.h"
#include "kernel/panic.h"
#include "kernel/types.h"
#include "mm/kmalloc.h"
#include "mm/page.h"
#include "mm/page_alloc.h"

/**
 * @addtogroup drivers
 * @{
 */

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
 * @brief Creates and registers the VGA console TTY device.
 *
 * Creates a TTY device named "tty0" that outputs to the VGA text console.
 * It allocates the output ring buffer and sets up the TTY structures. This
 * TTY is the primary console output device for the system.
 *
 * @note Panics if the ring buffer allocation fails. The console TTY is
 * essential for system operation.
 */
void vconsole_tty_init()
{
	struct tty* tty = kzalloc(sizeof(struct tty));
	tty->driver = &vconsole_driver;
	strncpy(tty->name, "tty0", 32);

	struct ring_buffer* rbo = &tty->output_buffer;
	rbo->buffer = get_free_pages(AF_KERNEL, RING_BUFFER_SIZE_PAGES);
	if (!rbo->buffer) {
		panic("Didn't get free pages");
	}
	rbo->size = RING_BUFFER_SIZE;
	spin_init(&rbo->lock);

	struct ring_buffer* rbi = &tty->input_buffer;
	rbi->buffer = get_free_pages(AF_KERNEL, RING_BUFFER_SIZE_PAGES);
	if (!rbi->buffer) {
		panic("Didn't get free pages");
	}
	rbi->size = RING_BUFFER_SIZE;
	spin_init(&rbi->lock);
	waitqueue_init(&rbi->readers);

	sem_init(&tty->write_lock, 1);
	register_tty(tty);
}

ssize_t vconsole_tty_write(struct tty* tty)
{
	struct ring_buffer* rb = &tty->output_buffer;
	ssize_t bytes_written = 0;

	sem_wait(&tty->write_lock);

	while (rb->head != rb->tail) {
		// screen_putchar(rb->buffer[rb->tail]);
		term_putchar(rb->buffer[rb->tail]);
		rb->tail = (rb->tail + 1) % rb->size;
		bytes_written++;
	}

	sem_signal(&tty->write_lock);

	return bytes_written;
}

/** @} */
