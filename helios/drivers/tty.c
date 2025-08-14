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

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

static LIST_HEAD(g_ttys);

struct file_ops tty_device_fops = {
	.write = tty_write,
	.read = nullptr,
	.open = tty_open,
	.close = nullptr,
};

// The collection of operations for our TTY device
struct inode_ops tty_device_ops = {
	.lookup = nullptr,
};

/*******************************************************************************
 * Private Function Prototypes
 *******************************************************************************/

/**
 * find_tty_by_name - Find a TTY device by its name
 * @name: The name of the TTY device to search for
 *
 * Return: Pointer to the TTY device if found, nullptr otherwise
 */
static struct tty* find_tty_by_name(const char* name);

/**
 * tty_fill_buffer - Fill a ring buffer with data from a source buffer
 * @rb: Pointer to the ring buffer to fill
 * @buffer: Source buffer containing data to copy
 * @count: Number of bytes to copy from the source buffer
 *
 * Return: Number of bytes successfully copied to the ring buffer
 */
static ssize_t tty_fill_buffer(struct ring_buffer* rb,
			       const char* buffer,
			       size_t count);

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * register_tty - Register a TTY device with the system
 * @tty: Pointer to the TTY device structure to register
 *
 * Adds the specified TTY device to the global list of available TTY devices.
 * This makes the TTY accessible for use by the system and applications.
 * The TTY structure must be properly initialized before calling this function.
 */
void register_tty(struct tty* tty)
{
	log_debug("Registered tty: '%s'", tty->name);
	list_add(&g_ttys, &tty->list);
}

void tty_init()
{
	serial_tty_init();

	struct tty* tty = nullptr;
	list_for_each_entry (tty, &g_ttys, list) {
		register_device(tty->name, &tty_device_fops);
	}
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

// This is the implementation for writing to the screen
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count)
{
	log_debug("tty_write: file=%p, buffer=%p, count=%zu",
		  (void*)file,
		  (void*)buffer,
		  count);

	struct tty* tty = file->private_data;
	struct ring_buffer* rb = &tty->output_buffer;

	ssize_t written = tty_fill_buffer(rb, buffer, count);

	add_work_item(tty_drain_output_buffer, tty);

	return written;
}

int tty_open(struct vfs_inode* inode, struct vfs_file* file)
{
	(void)inode;
	file->private_data = find_tty_by_name(file->dentry->name);
	return VFS_OK;
}

/*******************************************************************************
 * Private Function Definitions
 *******************************************************************************/

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

/**
 * tty_fill_buffer - Fill a ring buffer with data from a source buffer
 * @rb: Pointer to the ring buffer to fill
 * @buffer: Source buffer containing data to copy
 * @count: Number of bytes to copy from the source buffer
 *
 * Return: Number of bytes successfully copied to the ring buffer
 */
static ssize_t tty_fill_buffer(struct ring_buffer* rb,
			       const char* buffer,
			       size_t count)
{

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

	return (ssize_t)i;
}
