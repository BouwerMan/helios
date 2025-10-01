/**
 * @file arch/x86_64/pit.c
 *
 * Copyright (C) 2025  Dylan Parks
 *
 * This file is part of HeliOS
 *
 * HeliOS is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "arch/pit.h"
#include "arch/idt.h"
#include "arch/ports.h"
#include "kernel/timer.h"
#include "lib/log.h"

// Some IBM employee had a very fun time when designing this fucker.
static constexpr u32 PIT_CLK = 1193180;

u32 __pit_phase = 18;

/**
 * pit_handler() - Interrupt handler for the Programmable Interval Timer (PIT)
 * @r: Pointer to the CPU registers at the time of the interrupt
 */
void pit_handler(struct registers* r)
{
	(void)r;
	timer_handler();
}

/**
 * pit_phase() - Set the frequency of the PIT and initialize it
 * @hz: Desired frequency in Hertz
 */
static void pit_phase(u32 hz)
{
	__pit_phase = hz;
	u32 divisor = PIT_CLK / hz; /* Calculate our divisor */
	u8 low = (u8)(divisor & 0xFF);
	u8 high = (u8)((divisor >> 8) & 0xFF);
	outb(0x43, 0x36);	    /* Set our command byte 0x36 */
	outb(0x40, low);	    /* Set low byte of divisor */
	outb(0x40, high);	    /* Set high byte of divisor */
}

void pit_init(void)
{
	log_debug("Initializing PIT to 1000Hz");
	pit_phase(TIMER_HERTZ);
	isr_install_handler(IRQ0, pit_handler);
}
