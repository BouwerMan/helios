/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <kernel/types.h>
#include <stddef.h>

typedef u32 dev_t;

/**
 * struct chrdev - character-device range descriptor
 * @base:   First device number in this contiguous range.
 * @count:  Number of minors in the range (>=1). Covers [minor(base), minor(base)+count).
 * @name:   Canonical base name (e.g., "ttyS", "null"). Advisory only; not used by lookup.
 * @fops:   Immutable VFS operation table for this device family.
 * @drvdata:Driver-private pointer installed into struct vfs_file->private_data by .open (optional).
 * @flags:  Internal flags (e.g., CHRDEV_F_DEAD). Not ABI.
 *
 * A chrdev binds a contiguous minor range to a single ops table. If you need
 * different ops for different minors, register multiple chrdev ranges.
 */
struct chrdev {
	dev_t base;	  // Base dev_t for this range
	u16 count;	  // How many minors in this range
	const char* name; // Canonical name: "ttyS", "null", etc.
	const struct file_ops* fops;
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

int chrdev_lookup(dev_t dev,
		  const struct file_ops** fops_out,
		  void** drvdata_out,
		  dev_t* base_out,
		  size_t* count_out);
