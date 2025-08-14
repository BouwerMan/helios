/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>
#include <sys/types.h>

/**
 * vconsole_tty_init - Initialize the VGA console TTY device
 */
void vconsole_tty_init();

/**
 * vconsole_tty_write - Drain the TTY output buffer to the VGA console
 * @tty: Pointer to the TTY device whose output buffer to drain
 */
ssize_t vconsole_tty_write(struct tty* tty);
