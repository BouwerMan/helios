#pragma once
#include "../arch/x86_64/interrupts/idt.h"
#include <stdint.h>

void timer_init(void);
void timer_poll(void);
void timer_phase(int hz);
void sleep(uint64_t millis);
void timer_handler(struct registers* r);
