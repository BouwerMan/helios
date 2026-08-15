/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stddef.h>

/*
 * Both helpers are check-then-copy: they validate the user range against
 * the current task's VMAs, then memcpy(). This is not the fixup-table
 * approach Linux uses, so it is not atomic against another thread unmapping
 * the range mid-copy. HeliOS has no threads yet (one task, one address
 * space, no clone()), so there is currently no way to hit that race - but
 * the moment threads land, this assumption needs revisiting.
 */

#if !defined(__user)
#define __user
#endif

long copy_from_user(void* dst, const void __user* src, size_t len);
long copy_to_user(void __user* dst, const void* src, size_t len);
