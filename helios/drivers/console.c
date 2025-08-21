#include <drivers/console.h>
#include <drivers/device.h>
#include <drivers/serial.h>
#include <drivers/tty.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <kernel/semaphores.h>
#include <kernel/spinlock.h>
#include <liballoc.h>
#include <mm/page_alloc.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 * Global Variable Definitions
 *******************************************************************************/

struct file_ops console_device_fops = {
	.write = console_write,
};

static LIST_HEAD(g_console_sinks);
static semaphore_t g_console_sem;

struct console_sink {
	struct tty* tty;
	struct list_head list;
};

/*******************************************************************************
 * Public Function Definitions
 *******************************************************************************/

/**
 * console_init - Initialize the console subsystem
 */
void console_init()
{
	sem_init(&g_console_sem, 1);
	register_device("console", &console_device_fops);
}

void attach_tty_to_console(const char* name)
{
	struct tty* tty = find_tty_by_name(name);
	if (!tty) return;

	struct console_sink* sink = kmalloc(sizeof(struct console_sink));
	if (!sink) return;

	sink->tty = tty;
	list_add_tail(&g_console_sinks, &sink->list);
}

void detach_tty(const char* name)
{
	struct tty* tty = find_tty_by_name(name);
	if (!tty) return;
	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		if (sink->tty == tty) {
			list_del(&sink->list);
			kfree(sink);
			break;
		}
	}
}

ssize_t console_write(struct vfs_file* file, const char* buffer, size_t count)
{
	sem_wait(&file->dentry->inode->lock);

	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		__write_to_tty(sink->tty, buffer, count);
	}

	sem_signal(&file->dentry->inode->lock);

	return (ssize_t)count;
}

/**
 * console_flush - Flush output buffers for all registered console sinks
 */
void console_flush()
{
	struct console_sink* sink;
	list_for_each_entry (sink, &g_console_sinks, list) {
		tty_drain_output_buffer(sink->tty);
	}
}
