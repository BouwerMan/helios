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

#include <uapi/helios/errno.h>

#include "drivers/device.h"
#include "kernel/spinlock.h"
#include "lib/list.h"
#include "lib/log.h"
#include "mm/kmalloc.h"

struct major_info {
	bool used;
	char* label;
	struct hlist_head devlist;
};

static constexpr size_t CHRDEV_LIST_SIZE = (1u << 16);
struct major_info chrdevs_by_major[CHRDEV_LIST_SIZE];
spinlock_t chrdevs_lock;

static u16 next_major = 1;

static struct chrdev* chrdev_find(dev_t d);

void chrdevs_init()
{
	spin_init(&chrdevs_lock);
	for (size_t i = 0; i < CHRDEV_LIST_SIZE; i++) {
		struct major_info* m = &chrdevs_by_major[i];
		m->used = false;
		m->label = nullptr;
		INIT_HLIST_HEAD(&m->devlist);
	}
}

/**
 * alloc_chrdev_region() - Reserve a fresh major and a contiguous block of minors.
 * @base_out:  On success, receives mkdev(major, first_minor) for the reserved block.
 * @count:     Number of minors to reserve (>=1). Must satisfy minor+count <= 65536.
 * @name:      Advisory label for diagnostics (e.g., "ttyS"). Can be nullptr.
 *
 * Return: 0 on success,
 *         -EINVAL for invalid arguments,
 *         -ENOSPC if no majors are available.
 *
 * Notes:
 *  - This only reserves numbers; it does not publish a cdev. Call cdev_add() next.
 *  - Minors are reserved conceptually for your driver; the registry enforces
 *    non-overlap at cdev_add() time.
 */
int alloc_chrdev_region(dev_t* base_out, unsigned count, const char* name)
{
	if (!base_out || count == 0 || count > 65536) {
		return -EINVAL;
	}

	unsigned long flags;
	spin_lock_irqsave(&chrdevs_lock, &flags);

	// For now, we basically do 1 major per call of this function

	u16 start = next_major ? next_major : 1;
	u16 chosen = 0;
	for (u32 n = 0; n < 65535; ++n) {
		u16 cand = (u16)(start + n); // Handles wrapping with width
		if (cand == 0) continue;     // keep 0 special
		if (!chrdevs_by_major[cand].used) {
			chosen = cand;
			break;
		}
	}
	if (!chosen) return -ENOSPC;
	next_major = (u16)(chosen + 1);

	struct major_info* m = &chrdevs_by_major[chosen];
	if (m->used) {
		spin_unlock_irqrestore(&chrdevs_lock, flags);
		return -ENOSPC;
	}

	m->label = strdup(name ? name : "unknown");
	if (!m->label) {
		spin_unlock_irqrestore(&chrdevs_lock, flags);
		return -ENOMEM;
	}

	m->used = true;
	*base_out = MKDEV(chosen, 0);

	spin_unlock_irqrestore(&chrdevs_lock, flags);
	return 0;
}

/**
 * release_chrdev_region() - Release a previously reserved number block.
 * @base:  Base dev_t previously returned by alloc_chrdev_region().
 * @count: Matching count.
 *
 * Use this after cdev_del() when you no longer need the major/minor block.
 * Safe to call even if no cdev was added yet for the range.
 */
void release_chrdev_region(dev_t base, unsigned count)
{
	(void)count; // This doesn't mean anything yet

	unsigned long flags;
	spin_lock_irqsave(&chrdevs_lock, &flags);

	u16 major = MAJOR(base);
	struct major_info* m = &chrdevs_by_major[major];
	if (!m->used) {
		log_warn("release_chrdev_region: major %u not in use", major);
		spin_unlock_irqrestore(&chrdevs_lock, flags);
		return;
	}

	if (!hlist_empty(&m->devlist)) {
		log_warn("release_chrdev_region: major %u still has devices",
			 major);
		return;
	}

	m->used = false;
	kfree((void*)m->label);
	m->label = nullptr;

	spin_unlock_irqrestore(&chrdevs_lock, flags);
}

/**
 * chrdev_add() - Publish a character-device range into the registry.
 * @cdev:  Caller-provided descriptor. Fields base/count/name/fops/drvdata must be set.
 * @base:  First dev_t in the range. Must not overlap any existing registered range.
 * @count: Number of minors in the range (>=1).
 *
 * Return: 0 on success,
 *         -EINVAL if arguments are invalid,
 *         -EBUSY  if the requested range overlaps an existing cdev,
 *         -ENOENT if the major is not currently reserved.
 *
 * Concurrency:
 *  - After success, opens for any dev in the range may begin immediately.
 *  - The @cdev storage must remain valid until cdev_del() returns.
 */
int chrdev_add(struct chrdev* cdev, dev_t base, unsigned count)
{
	if (!cdev || !cdev->fops || count == 0 || count > 65536 ||
	    (MINOR(base) + count) > 65536) {
		return -EINVAL;
	}

	if (!hlist_unhashed(&cdev->hnode)) {
		return -EALREADY;
	}

	u16 major = MAJOR(base);
	u16 minor_start = MINOR(base);
	u16 minor_end = minor_start + (u16)count; // exclusive

	unsigned long flags;
	spin_lock_irqsave(&chrdevs_lock, &flags);

	struct major_info* m = &chrdevs_by_major[major];
	if (!m->used) {
		spin_unlock_irqrestore(&chrdevs_lock, flags);
		return -ENOENT;
	}

	struct chrdev* exist;
	hlist_for_each_entry (exist, &m->devlist, hnode) {
		u16 e_start = MINOR(exist->base);
		u32 e_end = (u32)e_start + (u32)exist->count; // exclusive
		u32 n_start = (u32)minor_start;
		u32 n_end = (u32)minor_end;

		if (n_start < e_end && n_end > e_start) {
			spin_unlock_irqrestore(&chrdevs_lock, flags);
			return -EBUSY;
		}
	}

	cdev->base = base;
	cdev->count = (u16)count;

	hlist_add_head(&m->devlist, &cdev->hnode);

	spin_unlock_irqrestore(&chrdevs_lock, flags);

	return 0;
}

/**
 * chrdev_del() - Unpublish a character-device range from the registry.
 * @cdev: The descriptor previously added with cdev_add().
 *
 * Effects:
 *  - New opens for this range will fail with -ENODEV.
 *  - Existing open file descriptors continue per driver policy; ensure your
 *    .release/.flush paths tolerate late teardown.
 *  - After cdev_del() returns, you may free @cdev.
 */
void chrdev_del(struct chrdev* cdev)
{
	if (!cdev) return;

	unsigned long flags;
	spin_lock_irqsave(&chrdevs_lock, &flags);

	hlist_del_init(&cdev->hnode);

	spin_unlock_irqrestore(&chrdevs_lock, flags);
}

/**
 * chrdev_lookup() - Resolve a device number to driver hooks.
 * @dev:         Device number (major,minor) to resolve.
 * @fops_out:    If non-NULL, receives the ops table pointer.
 * @drvdata_out: If non-NULL, receives the driver-private pointer associated with the range.
 * @base_out:    If non-NULL, receives the base dev_t of the range.
 * @count_out:   If non-NULL, receives the size of the range.
 *
 * Return: 0 on success, -ENODEV if no cdev covers @dev.
 *
 * Usage:
 *  - VFS should call this when opening an inode with filetype==CHAR_DEV,
 *    using inode->rdev as @dev. On success, install *fops_out onto the
 *    struct vfs_file and optionally stash *drvdata_out into file->private_data
 *    before invoking .open.
 *
 * Notes:
 *  - This API intentionally returns copies, not a borrowed chrdev pointer,
 *    to avoid lifetime and UAF hazards in the fast path.
 */
int chrdev_lookup(dev_t dev,
		  const struct file_ops** fops_out,
		  void** drvdata_out,
		  dev_t* base_out,
		  size_t* count_out)
{
	unsigned long flags;
	spin_lock_irqsave(&chrdevs_lock, &flags);

	struct chrdev* cdev = chrdev_find(dev);
	if (!cdev) {
		spin_unlock_irqrestore(&chrdevs_lock, flags);
		return -ENODEV;
	}

	if (fops_out) {
		*fops_out = cdev->fops;
	}
	if (drvdata_out) {
		*drvdata_out = cdev->drvdata;
	}
	if (base_out) {
		*base_out = cdev->base;
	}
	if (count_out) {
		*count_out = cdev->count;
	}

	spin_unlock_irqrestore(&chrdevs_lock, flags);
	return 0;
}

// Expects lock to be held
static struct chrdev* chrdev_find(dev_t d)
{
	u16 major = MAJOR(d);
	u16 minor = MINOR(d);

	struct chrdev* cdev;
	hlist_for_each_entry (cdev, &chrdevs_by_major[major].devlist, hnode) {
		u16 cminor = MINOR(cdev->base);
		if (minor >= cminor && minor < (cminor + cdev->count)) {
			return cdev;
		}
	}

	return nullptr;
}
