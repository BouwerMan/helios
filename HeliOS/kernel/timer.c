#include <kernel/asm.h>
#include <kernel/sys.h>
#include <kernel/timer.h>
#include <stdio.h>

const int PIT_CLK = 1193180;

/* This will keep track of how many ticks that the system
 *  has been running for */
unsigned int ticks = 0;
unsigned long ticker = 0;
uint64_t countdown = 0;
static uint32_t phase = 18;

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

// NOTE: I have changed the timer phase so now it fires every ~1ms
//
/* Handles the timer. In this case, it's very simple: We
 *  increment the 'timer_ticks' variable every time the
 *  timer fires. By default, the timer fires 18.222 times
 *  per second. Why 18.222Hz? Some engineer at IBM must've
 *  been smoking something funky */
void timer_handler(struct registers* r)
{
    /* Increment our 'tick count' */
    ticks++;
    // Decrement sleep countdown, should be every 1ms
    if (countdown > 0) countdown--;
    /* Every 18 clocks (approximately 1 second), we will
     *  display a message on the screen */
    if (ticks % phase == 0) {
        ticker++;
    }
}

/* Waits until the timer at least one time.
 * Added optimize attribute to stop compiler from
 * optimizing away the while loop and causing the kernel to hang. */
void __attribute__((optimize("O0"))) timer_poll()
{
    while (0 == ticks) { }
}

void sleep(uint32_t millis)
{
    countdown = millis;
    while (countdown > 0) {
        halt();
    }
}

/* Sets up the system clock by installing the timer handler
 *  into IRQ0 */
void timer_init()
{
    puts("Initializing timer to 1000Hz");
    /* Installs 'timer_handler' to IRQ0 */
    install_isr_handler(IRQ0, timer_handler);
    timer_phase(1000);
}
