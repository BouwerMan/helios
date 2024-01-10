#pragma once

void timer_phase(int hz);
void timer_handler(struct irq_regs* r);
void timer_init();
