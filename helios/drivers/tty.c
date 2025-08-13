#include <drivers/device.h>
#include <drivers/fs/vfs.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <kernel/dmesg.h>
#include <kernel/screen.h>
#include <stddef.h>
#include <sys/types.h>
#include <util/log.h>

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

void tty_init()
{
	register_device("stdout", &tty_device_fops);
}

// This is the implementation for writing to the screen
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count)
{
	log_debug("tty_write: file=%p, buffer=%p, count=%zu",
		  (void*)file,
		  (void*)buffer,
		  count);
	(void)file;

	dmesg_enqueue(buffer, count); // Write to the kernel log

	// for (size_t i = 0; i < count; i++) {
	// screen_putchar(buffer[i]);
	// write_serial(buffer[i]);
	// }
	return (ssize_t)count; // Return the number of bytes written
}

// TODO: Implement a tty_read that gets keyboard input
