#include <drivers/fs/vfs.h>
#include <kernel/screen.h>
#include <stddef.h>
#include <sys/types.h>

// This is the implementation for writing to the screen
ssize_t tty_write(struct vfs_file* file, const char* buffer, size_t count)
{
	(void)file;
	for (size_t i = 0; i < count; i++) {
		screen_putchar(buffer[i]); // Or write_serial() or both!
	}
	return (ssize_t)count; // Return the number of bytes written
}

// TODO: Implement a tty_read that gets keyboard input

struct file_ops tty_device_fops = {
	.write = tty_write,
	.read = NULL,
	.open = NULL,
	.close = NULL,
};

// The collection of operations for our TTY device
struct inode_ops tty_device_ops = {
	.lookup = NULL,
};
