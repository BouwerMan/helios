/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <stddef.h>

typedef u32 dev_t;

/**
 * @brief Describes a contiguous character-device number range.
 *
 * A chrdev binds a contiguous minor range to one ops table. Register a
 * separate chrdev range to use different ops for different minors.
 */
struct chrdev {
	/** @brief First device number in this range. */
	dev_t base; // Base dev_t for this range
	/** @brief Number of minors in the range (at least 1). */
	u16 count; // How many minors in this range
	/** @brief Canonical base name, e.g. "ttyS". Advisory only. */
	const char* name; // Canonical name: "ttyS", "null", etc.
	/** @brief Immutable VFS operation table for this device family. */
	const struct file_ops* fops;
	/** @brief Driver-private pointer for the open file, if any. */
	void* drvdata;
	struct hlist_node hnode;
};

static inline u16 MAJOR(dev_t dev)
{
	return (u16)((dev >> 16) & 0xFFFF);
}

static inline u16 MINOR(dev_t dev)
{
	return (u16)(dev & 0xFFFF);
}

static inline dev_t MKDEV(u16 major, u16 minor)
{
	return (((dev_t)major << 16) | minor);
}

void chrdevs_init();

int alloc_chrdev_region(dev_t* base_out, unsigned count, const char* name);
void release_chrdev_region(dev_t base, unsigned count);

int chrdev_add(struct chrdev* c, dev_t dev, unsigned count);
void chrdev_del(struct chrdev* c);

int chrdev_lookup(dev_t dev, const struct file_ops** fops_out, void** drvdata_out, dev_t* base_out, size_t* count_out);
