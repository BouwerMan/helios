#pragma once
#include "../arch/x86_64/interrupts/idt.h"
#include <stdint.h>

void timer_phase(int hz);
void timer_handler(struct registers* r);
void timer_poll();
void sleep(uint32_t millis);
void timer_init();
