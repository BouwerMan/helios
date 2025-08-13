#include <drivers/device.h>
#include <drivers/fs/vfs.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <kernel/dmesg.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <kernel/work_queue.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <util/log.h>

static struct tty* g_active_tty = nullptr;
static LIST_HEAD(g_ttys);

struct file_ops tty_device_fops = {
	.write = tty_write,
	.read = NULL,
	.open = nullptr, // TODO: needed?
	.close = NULL,
};

// The collection of operations for our TTY device
struct inode_ops tty_device_ops = {
	.lookup = NULL,
};

void register_tty(struct tty* tty)
{
	log_debug("Registered tty: '%s'", tty->name);
	list_add(&g_ttys, &tty->list);
}

static struct tty* find_tty_by_name(const char* name)
{
	struct tty* tty = nullptr;
	list_for_each_entry (tty, &g_ttys, list) {
		if (!strcmp(tty->name, name)) {
			return tty;
		}
	}
	return nullptr;
}

void tty_init()
{
	serial_tty_init();

	g_active_tty = find_tty_by_name("ttyS0");
	register_device("ttyS0", &tty_device_fops);
	// register_device("stdout", &tty_device_fops);
}

void tty_drain_output_buffer(void* data)
{
	struct tty* tty_to_drain = (struct tty*)data;

	// Check if the TTY has a driver and a write function
	if (tty_to_drain && tty_to_drain->driver &&
	    tty_to_drain->driver->write) {
		// Call the specific driver's write function (e.g., serial_write)
		tty_to_drain->driver->write(tty_to_drain);
	}
}

// void tty_fill_buffer(const char* )
// {
//
// }

// This is the implementation for writing to the screen
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count)
{
	log_debug("tty_write: file=%p, buffer=%p, count=%zu",
		  (void*)file,
		  (void*)buffer,
		  count);
	// TODO: Find tty device from file. Probably by linking the 2 when
	// tty_open is called.
	(void)file;

	struct ring_buffer* rb = &g_active_tty->output_buffer;
	size_t i = 0;
	spinlock_acquire(&rb->lock);

	// TODO: Make sure there is room

	for (; i < count; i++) {
		rb->buffer[rb->head] = buffer[i];
		rb->head = (rb->head + 1) % rb->size;

		if (rb->head == rb->tail) {
			// optional: drop oldest char or pause until consumed
			rb->tail = (rb->tail + 1) % rb->size;
		}
	}

	spinlock_release(&rb->lock);

	add_work_item(tty_drain_output_buffer, g_active_tty);

	return (ssize_t)i; // Return the number of bytes written
}

// TODO: Implement a tty_read that gets keyboard input
