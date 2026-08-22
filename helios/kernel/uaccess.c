/**
 * @file kernel/uaccess.c
 *
 * Copyright (C) 2026 Dylan Parks
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

#include "kernel/uaccess.h"
#include "arch/idt.h"
#include "kernel/assert.h"
#include "kernel/tasks/scheduler.h"
#include "mm/address_space.h"
#include "mm/page.h"

#include <uapi/helios/errno.h>
#include <uapi/helios/mman.h>

/**
 * @brief Rejects NULL, overflowing, or kernel-half arguments cheaply.
 *
 * @param k Kernel-side pointer of the copy.
 * @param u User-side pointer of the copy.
 * @param len Length of the range in bytes.
 *
 * @return true if the range is worth checking with check_access(), false
 * to reject it outright.
 */
static bool validate_args(const void* k, const void __user* u, size_t len)
{
	if (!k || !u) {
		return false;
	}

	uptr k_ptr = (uptr)k;
	uptr u_ptr = (uptr)u;

	// Checko for pointer wrap-around
	if (k_ptr + len < k_ptr) {
		return false;
	}

	// Ensure starting point is valid, then make check if length crosses
	// boundary (effectively checks overflow).
	if (u_ptr >= HHDM_OFFSET || len > HHDM_OFFSET - u_ptr) {
		return false;
	}

	return true;
}

/**
 * @brief Validates and copies a range out of userspace.
 *
 * @param dst Kernel destination buffer.
 * @param src User source pointer.
 * @param len Number of bytes to copy.
 *
 * @return 0 on success, or -EFAULT if the source or length is rejected.
 *
 * @note Call this function from task context only. Do not call it from
 * an IRQ handler.
 */
long copy_from_user(void* dst, const void __user* src, size_t len)
{
	kassert(!is_in_interrupt_context(), "copy_from_user called from interrupt context");

	if (len == 0) {
		return 0;
	}

	if (!validate_args(dst, src, len)) {
		return -EFAULT;
	}

	struct address_space* vas = get_current_task()->vas;

	down_read(&vas->vma_lock);
	vaddr_t ptr = (vaddr_t)src;
	for (vaddr_t v = align_down_page(ptr); v < ptr + len; v += PAGE_SIZE) {
		int access = check_access(vas, v, true, false, false);
		if (access < 0) {
			up_read(&vas->vma_lock);
			return -EFAULT;
		}
	}
	memcpy(dst, src, len);
	up_read(&vas->vma_lock);

	return 0;
}

/**
 * @brief Validates and copies a range into userspace.
 *
 * @param dst User destination pointer.
 * @param src Kernel source buffer.
 * @param len Number of bytes to copy.
 *
 * @return 0 on success, or -EFAULT if the destination or length is
 * rejected.
 *
 * @note Call this function from task context only. Do not call it from
 * an IRQ handler.
 */
long copy_to_user(void __user* dst, const void* src, size_t len)
{
	kassert(!is_in_interrupt_context(), "copy_to_user called from interrupt context");

	if (len == 0) {
		return 0;
	}

	if (!validate_args(src, dst, len)) {
		return -EFAULT;
	}

	struct address_space* vas = get_current_task()->vas;

	down_read(&vas->vma_lock);
	vaddr_t ptr = (vaddr_t)dst;
	for (vaddr_t v = align_down_page(ptr); v < ptr + len; v += PAGE_SIZE) {
		int access = check_access(vas, v, false, true, false);
		if (access < 0) {
			up_read(&vas->vma_lock);
			return -EFAULT;
		}
	}
	memcpy(dst, src, len);
	up_read(&vas->vma_lock);

	return 0;
}

#if defined(HELIOS_TESTS)
#include "kernel/ktest.h"
KTEST(test_uaccess_address_validation)
{
	size_t len = 1;
	void* valid_k = (void*)0x1000;
	void* valid_u = (void*)0x1000;

	// NULL rejected on either side
	KTEST_ASSERT_FALSE(validate_args(NULL, valid_u, len));
	KTEST_ASSERT_FALSE(validate_args(valid_k, NULL, len));

	// kernel pointer overflow: k + len wraps past UINTPTR_MAX
	KTEST_ASSERT_FALSE(validate_args((void*)(UINTPTR_MAX - 10), valid_u, 20));

	// user range overflows into the kernel half without u itself being there
	KTEST_ASSERT_FALSE(validate_args(valid_k, (void*)(HHDM_OFFSET - 5), 10));

	// user pointer already at/above the kernel half
	KTEST_ASSERT_FALSE(validate_args(valid_k, (void*)HHDM_OFFSET, len));

	return 0;
}

KTEST(test_copy_from_user)
{
	int rc = 1;

	struct address_space* vas = get_current_task()->vas;
	uptr ustart = 0x10000000;
	uptr uend = ustart + PAGE_SIZE;

	KTEST_ASSERT_EQ(map_region(vas,
				   (struct mr_file) { 0 },
				   ustart,
				   uend,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS),
			0);

	char msg[] = "funny little string";
	size_t len = ARRAY_SIZE(msg);
	// faults the page in, proving it's really live, not just registered
	memcpy((void*)ustart, msg, len);

	char dst[len];
	memset(dst, 0, len);
	KTEST_ASSERT_EQ_GOTO(copy_from_user(dst, (void*)ustart, len), 0, err_unmap_region);
	KTEST_ASSERT_EQ_GOTO(memcmp(dst, msg, len), 0, err_unmap_region);

	rc = 0;

err_unmap_region:
	struct memory_region* mr = get_region(vas, ustart);
	unmap_region(vas, mr);
	return rc;
}

KTEST(test_copy_to_user)
{
	int rc = 1;

	struct address_space* vas = get_current_task()->vas;
	uptr ustart = 0x10000000;
	uptr uend = ustart + PAGE_SIZE;

	KTEST_ASSERT_EQ(map_region(vas,
				   (struct mr_file) { 0 },
				   ustart,
				   uend,
				   PROT_READ | PROT_WRITE,
				   MAP_PRIVATE | MAP_ANONYMOUS),
			0);

	char msg[] = "another funny string";
	size_t len = ARRAY_SIZE(msg);
	char dst[len];
	memset(dst, 0, len);

	KTEST_ASSERT_EQ_GOTO(copy_to_user((void*)ustart, msg, len), 0, err_unmap_region);

	KTEST_ASSERT_EQ_GOTO(copy_from_user(dst, (void*)ustart, len), 0, err_unmap_region);
	KTEST_ASSERT_EQ_GOTO(memcmp(dst, msg, len), 0, err_unmap_region);

	rc = 0;

err_unmap_region:
	struct memory_region* mr = get_region(vas, ustart);
	unmap_region(vas, mr);
	return rc;
}

KTEST(test_copy_to_user_readonly_rejected)
{
	int rc = 1;

	struct address_space* vas = get_current_task()->vas;
	uptr ustart = 0x10001000;
	uptr uend = ustart + PAGE_SIZE;

	KTEST_ASSERT_EQ(map_region(vas, (struct mr_file) { 0 }, ustart, uend, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS),
			0);

	char msg[] = "should not land";
	KTEST_ASSERT_EQ_GOTO(copy_to_user((void*)ustart, msg, ARRAY_SIZE(msg)), -EFAULT, err_unmap_region);

	rc = 0;

err_unmap_region:
	struct memory_region* mr = get_region(vas, ustart);
	unmap_region(vas, mr);
	return rc;
}

#endif
