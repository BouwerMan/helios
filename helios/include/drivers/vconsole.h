/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <drivers/tty.h>
#include <kernel/types.h>

/**
 * @addtogroup drivers
 * @{
 */

/**
 * @brief Creates and registers the VGA console TTY device.
 */
void vconsole_tty_init();

/**
 * @brief Drains the TTY output buffer to the VGA console.
 *
 * @param tty Pointer to the TTY device whose output buffer to drain.
 */
ssize_t vconsole_tty_write(struct tty* tty);

/** @} */
