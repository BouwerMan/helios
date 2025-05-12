#pragma once
#include "../arch/x86_64/interrupts/idt.h"
#include <stdint.h>

#define TIMER_HERTZ	    1000
#define millis_to_ticks(ms) ((((uint64_t)(ms) * TIMER_HERTZ) + 999) / 1000)

void timer_init(void);
void timer_poll(void);
void timer_phase(int hz);
void sleep(uint64_t millis);
void timer_handler(struct registers* r);
