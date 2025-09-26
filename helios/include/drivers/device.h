/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <stddef.h>

static constexpr size_t DEVICE_MAX_NAME = 32; // Max length of device name

struct device {
	struct file_ops* fops;	    // Operations for this device
	struct list_head list;	    // To link into the global list
	char name[DEVICE_MAX_NAME]; // e.g., "stdin", "tty"
};

void register_device(const char* name, struct file_ops* fops);
