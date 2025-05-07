#include <drivers/serial.h>
#include <kernel/dmesg.h>
#include <kernel/memory/pmm.h>
#include <kernel/memory/vmm.h>
#include <kernel/screen.h>
#include <kernel/spinlock.h>
#include <kernel/tasks/scheduler.h>
#include <util/log.h>
#define DMESG_BUFFER_SIZE 0x10000

char log_buffer[DMESG_BUFFER_SIZE];
size_t log_head = 0; // Where new messages are added
size_t log_tail = 0; // Where we consume from
spinlock_t log_lock;

struct task* dmesg_task = NULL;

void dmesg_init()
{
	spinlock_init(&log_lock);
	dmesg_task = new_task((void*)dmesg_task_entry);

	log_debug("Setting log mode to use dmesg (LOG_BUFFERED)");
	set_log_mode(LOG_BUFFERED);
}

void dmesg_enqueue(const char* str, size_t len)
{
	spinlock_acquire(&log_lock);

	for (size_t i = 0; i < len; i++) {
		log_buffer[log_head] = str[i];
		log_head = (log_head + 1) % DMESG_BUFFER_SIZE;

		if (log_head == log_tail) {
			// optional: drop oldest char or pause until consumed
			log_tail = (log_tail + 1) % DMESG_BUFFER_SIZE;
		}
	}
	spinlock_release(&log_lock);

	dmesg_wake();
}

void dmesg_task_entry(void)
{
	while (1) {
		spinlock_acquire(&log_lock);

		while (log_head != log_tail) {
			char c = log_buffer[log_tail];
			log_tail = (log_tail + 1) % DMESG_BUFFER_SIZE;
			spinlock_release(&log_lock);

			// output
			write_serial(c);
			screen_putchar(c);

			spinlock_acquire(&log_lock);
		}

		spinlock_release(&log_lock);
		dmesg_wait();
	}
}

static volatile bool data_ready = false;

void dmesg_wait()
{
	while (!data_ready) {
		yield();
	}
	data_ready = false;
}

void dmesg_wake()
{
	data_ready = true;
	dmesg_task->state = READY;
}
