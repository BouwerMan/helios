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

/**
 * @addtogroup drivers
 * @{
 */

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
 * @brief Reserves a new major number and a contiguous block of minors.
 *
 * @param base_out On success, receives mkdev(major, first_minor) for the
 * reserved block.
 * @param count Number of minors to reserve. Must be at least 1, and
 * minor+count must not exceed 65536.
 * @param name Advisory label for diagnostics, e.g. "ttyS". Can be nullptr.
 *
 * @return 0 on success, -EINVAL for invalid arguments, -ENOSPC if no majors
 * are available.
 *
 * This only reserves numbers. It does not publish a cdev; call cdev_add()
 * next. The registry enforces non-overlap of minors at cdev_add() time.
 */
int alloc_chrdev_region(dev_t* base_out, unsigned count, const char* name)
{
	if (!base_out || count == 0 || count > 65536) {
		return -EINVAL;
	}

	spin_guard(&chrdevs_lock);

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
	if (m->used) return -ENOSPC;

	m->label = strdup(name ? name : "unknown");
	if (!m->label) return -ENOMEM;

	m->used = true;
	*base_out = MKDEV(chosen, 0);

	return 0;
}

/**
 * @brief Releases a previously reserved device number block.
 *
 * @param base Base dev_t previously returned by alloc_chrdev_region().
 * @param count Matching count.
 *
 * Call this after cdev_del(), once the major/minor block is no longer
 * needed. It is safe to call even if no cdev was added for the range.
 */
void release_chrdev_region(dev_t base, unsigned count)
{
	(void)count; // This doesn't mean anything yet

	spin_guard(&chrdevs_lock);

	u16 major = MAJOR(base);
	struct major_info* m = &chrdevs_by_major[major];
	if (!m->used) {
		log_warn("release_chrdev_region: major %u not in use", major);
		return;
	}

	if (!hlist_empty(&m->devlist)) {
		log_warn("release_chrdev_region: major %u still has devices", major);
		return;
	}

	m->used = false;
	kfree((void*)m->label);
	m->label = nullptr;
}

/**
 * @brief Publishes a character-device range into the registry.
 *
 * @param cdev Caller-provided descriptor. The base, count, name, fops, and
 * drvdata fields must be set.
 * @param base First dev_t in the range. Must not overlap any existing
 * registered range.
 * @param count Number of minors in the range (at least 1).
 *
 * @return 0 on success, -EINVAL if arguments are invalid, -EBUSY if the
 * range overlaps an existing cdev, -ENOENT if the major is not reserved.
 *
 * After success, opens for any device in the range may begin immediately.
 * The @p cdev storage must remain valid until cdev_del() returns.
 */
int chrdev_add(struct chrdev* cdev, dev_t base, unsigned count)
{
	if (!cdev || !cdev->fops || count == 0 || count > 65536 || (MINOR(base) + count) > 65536) {
		return -EINVAL;
	}

	if (!hlist_unhashed(&cdev->hnode)) {
		return -EALREADY;
	}

	u16 major = MAJOR(base);
	u16 minor_start = MINOR(base);
	u16 minor_end = minor_start + (u16)count; // exclusive

	spin_guard(&chrdevs_lock);

	struct major_info* m = &chrdevs_by_major[major];
	if (!m->used) return -ENOENT;

	struct chrdev* exist;
	hlist_for_each_entry (exist, &m->devlist, hnode) {
		u16 e_start = MINOR(exist->base);
		u32 e_end = (u32)e_start + (u32)exist->count; // exclusive
		u32 n_start = (u32)minor_start;
		u32 n_end = (u32)minor_end;

		if (n_start < e_end && n_end > e_start) return -EBUSY;
	}

	cdev->base = base;
	cdev->count = (u16)count;

	hlist_add_head(&m->devlist, &cdev->hnode);

	return 0;
}

/**
 * @brief Unpublishes a character-device range from the registry.
 *
 * @param cdev The descriptor previously added with cdev_add().
 *
 * New opens for this range fail with -ENODEV. Existing open file
 * descriptors continue per driver policy, so the .release and .flush paths
 * must tolerate late teardown. The caller may free @p cdev once this
 * function returns.
 */
void chrdev_del(struct chrdev* cdev)
{
	if (!cdev) return;

	spin_guard(&chrdevs_lock);

	hlist_del_init(&cdev->hnode);
}

/**
 * @brief Resolves a device number to its driver hooks.
 *
 * @param dev Device number (major, minor) to resolve.
 * @param fops_out If non-NULL, receives the ops table pointer.
 * @param drvdata_out If non-NULL, receives the driver-private pointer for
 * the range.
 * @param base_out If non-NULL, receives the base dev_t of the range.
 * @param count_out If non-NULL, receives the size of the range.
 *
 * @return 0 on success, -ENODEV if no cdev covers @p dev.
 *
 * The VFS calls this when it opens an inode with filetype==CHAR_DEV, using
 * inode->rdev as @p dev. On success, it installs *fops_out onto the struct
 * vfs_file, and may stash *drvdata_out into file->private_data before it
 * calls .open.
 *
 * @note This function returns copies, not a borrowed chrdev pointer, to
 * avoid lifetime and use-after-free hazards in the fast path.
 */
int chrdev_lookup(dev_t dev, const struct file_ops** fops_out, void** drvdata_out, dev_t* base_out, size_t* count_out)
{
	spin_guard(&chrdevs_lock);

	struct chrdev* cdev = chrdev_find(dev);
	if (!cdev) return -ENODEV;

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

/** @} */
