#include <drivers/vconsole.h>
#include <kernel/panic.h>
#include <kernel/screen.h>
#include <mm/page_alloc.h>
#include <stdlib.h>
#include <sys/types.h>

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
	struct tty* tty = kzmalloc(sizeof(struct tty));
	tty->driver = &vconsole_driver;
	strncpy(tty->name, "tty0", 32);

	struct ring_buffer* rb = &tty->output_buffer;
	rb->buffer = get_free_pages(AF_KERNEL, RING_BUFFER_SIZE_PAGES);
	if (!rb->buffer) {
		panic("Didn't get free pages");
	}
	rb->size = RING_BUFFER_SIZE;
	spinlock_init(&rb->lock);

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

	spinlock_acquire(&rb->lock);

	while (rb->head != rb->tail) {
		screen_putchar(rb->buffer[rb->tail]);
		rb->tail = (rb->tail + 1) % rb->size;
		bytes_written++;
	}

	spinlock_release(&rb->lock);

	return bytes_written;
}
