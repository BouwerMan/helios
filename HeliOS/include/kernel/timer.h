#pragma once
#include <kernel/interrupts.h>
#include <stdint.h>

void timer_phase(int hz);
void timer_handler(struct irq_regs* r);
void timer_poll();
void sleep(uint32_t millis);
void timer_init();
