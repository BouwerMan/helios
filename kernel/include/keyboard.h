#pragma once
#include <interrupts.h>

void keyboard_handler(struct irq_regs* r);
void keyboard_init();
