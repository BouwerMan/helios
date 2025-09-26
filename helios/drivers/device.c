/**
 * @file drivers/device.c
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

#include <drivers/device.h>
#include <lib/list.h>
#include <lib/log.h>
#include <mm/kmalloc.h>

LIST_HEAD(g_registered_devices);

void register_device(const char* name, struct file_ops* fops)
{
	struct device* dev = kzalloc(sizeof(struct device));
	if (!dev) {
		log_error("Failed to allocate device");
		return;
	}

	strncpy(dev->name, name, DEVICE_MAX_NAME - 1);

	dev->fops = fops;

	list_add(&g_registered_devices, &dev->list);
}
