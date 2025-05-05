#include "../arch/x86_64/ports.h"
#include <kernel/tasks/scheduler.h>
#include <kernel/timer.h>
#include <util/log.h>

// Some IBM employee had a very fun time when designing this fucker.
const int PIT_CLK = 1193180;

/* This will keep track of how many ticks that the system
 *  has been running for */
unsigned int ticks = 0;
unsigned long ticker = 0;
uint64_t countdown = 0;
static uint32_t phase = 18;

extern volatile bool need_reschedule;

/**
 * Sets the timer phase by configuring the Programmable Interval Timer (PIT).
 *
 * This function calculates the divisor based on the desired frequency (in Hz)
 * and programs the PIT to generate interrupts at the specified frequency.
 *
 * @param hz The desired frequency in hertz (Hz).
 */
void timer_phase(int hz)
{
	phase = hz;
	int divisor = PIT_CLK / hz; /* Calculate our divisor */
	uint8_t low = (uint8_t)(divisor & 0xFF);
	uint8_t high = (uint8_t)((divisor >> 8) & 0xFF);
	outb(0x43, 0x36); /* Set our command byte 0x36 */
	outb(0x40, low);  /* Set low byte of divisor */
	outb(0x40, high); /* Set high byte of divisor */
}

/**
 * Handles the timer interrupt.
 *
 * This function is called whenever the timer fires. It increments the global
 * tick count and manages the sleep countdown. Additionally, it performs an
 * action every `phase` ticks, such as updating a ticker variable.
 *
 * @param r Unused parameter representing the CPU registers at the time of the interrupt.
 */
void timer_handler(struct registers* r)
{
	(void)r;
	/* Increment our 'tick count' */
	ticks++;
	// Decrement sleep countdown, should be every 1ms
	if (countdown > 0) countdown--;

	// Think this means this is true every 20 ms
	if (ticks % SCHEDULER_TIME == 0) {
		need_reschedule = true;
	}

	if (ticks % phase == 0) {
		ticker++;
	}
}

/* Waits until the timer at least one time.
 * Added optimize attribute to stop compiler from
 * optimizing away the while loop and causing the kernel to hang. */
void __attribute__((optimize("O0"))) timer_poll(void)
{
	while (0 == ticks) {
	}
}

/**
 * @brief Suspends execution of the current thread for a specified duration.
 *
 * This function blocks the thread by setting a countdown timer and halting
 * execution in a loop until the countdown reaches zero. The countdown is
 * decremented externally by the timer interrupt handler.
 *
 * @param millis The duration to sleep, in milliseconds.
 */
void sleep(uint64_t millis)
{
	countdown = millis;
	while (countdown > 0) {
		halt();
	}
}

/**
 * Initializes the system timer to generate interrupts at a frequency of 1000Hz.
 *
 * This function sets up the timer by installing the `timer_handler` as the
 * interrupt service routine (ISR) for IRQ0 and configuring the timer phase.
 * The timer is essential for task scheduling and timekeeping in the system.
 */
void timer_init(void)
{
	log_debug("Initializing timer to 1000Hz");
	/* Installs 'timer_handler' to IRQ0 */
	install_isr_handler(IRQ0, timer_handler);
	timer_phase(1000);
}
