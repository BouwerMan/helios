/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "kernel/assert.h"
#include "kernel/types.h"

static constexpr u32 FNV_PRIME_32 = 0x01000193; ///< The FNV prime constant.
static constexpr u32 FNV_OFFSET_32 =
	0x811c9dc5; ///< The FNV offset basis constant.

// FNV-1a hash for strings, returns a value in the range [0, 2^bits - 1]
static inline u32 hash_name32(const char* name, int bits)
{
	if (!name) {
		return 0;
	}

	u32 hash = FNV_OFFSET_32;
	for (const unsigned char* p = (const unsigned char*)name; *p; ++p) {
		hash ^= (u32)(*p);
		hash *= FNV_PRIME_32;
	}

	kassert(bits > 0 && bits < 32, "hash_index: bits out of range");
	u32 mask = (1u << bits) - 1u;

	return hash & mask;
}
