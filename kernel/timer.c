#include <asm.h>
#include <interrupts.h>
#include <stdio.h>

const int PIT_CLK = 1193180;

/* This will keep track of how many ticks that the system
 *  has been running for */
int ticks = 0;
unsigned long ticker = 0;

void timer_phase(int hz)
{
        int divisor = PIT_CLK / hz; /* Calculate our divisor */
        outb(0x43, 0x36); /* Set our command byte 0x36 */
        outb(0x40, divisor & 0xFF); /* Set low byte of divisor */
        outb(0x40, (divisor >> 8) & 0xFF); /* Set high byte of divisor */
}

/* Handles the timer. In this case, it's very simple: We
 *  increment the 'timer_ticks' variable every time the
 *  timer fires. By default, the timer fires 18.222 times
 *  per second. Why 18.222Hz? Some engineer at IBM must've
 *  been smoking something funky */
void timer_handler(struct irq_regs* r)
{
        /* Increment our 'tick count' */
        ticks++;
        /* Every 18 clocks (approximately 1 second), we will
         *  display a message on the screen */
        if (ticks % 18 == 0) {
                ticker++;
                // puts("One second has passed");
        }
}

/* Waits until the timer at least one time.
 * Added optimize attribute to stop compiler from
 * optimizing away the while loop and causing the kernel to hang. */
void __attribute__((optimize("O0"))) timer_poll()
{
        while (0 == ticks) { }
}

/* Sets up the system clock by installing the timer handler
 *  into IRQ0 */
void timer_init()
{
        /* Installs 'timer_handler' to IRQ0 */
        irq_install_handler(0, timer_handler);
        // timer_phase(100);
}
