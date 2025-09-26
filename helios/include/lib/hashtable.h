/**
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2025 Dylan Parks
 *
 * This file is a derivative work based on the Linux kernel file:
 * include/linux/hashtable.h
 * include/linux/hash.h
 *
 * The original file from the Linux kernel is licensed under GPL-2.0
 * (SPDX-License-Identifier: GPL-2.0) and is copyrighted by the
 * Linux kernel contributors.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <kernel/kmath.h>
#include <kernel/types.h>
#include <lib/list.h>

/*
 * This hash multiplies the input by a large odd number and takes the
 * high bits.  Since multiplication propagates changes to the most
 * significant end only, it is essential that the high bits of the
 * product be used for the hash value.
 *
 * Chuck Lever verified the effectiveness of this technique:
 * http://www.citi.umich.edu/techreports/reports/citi-tr-00-1.pdf
 *
 * Although a random odd number will do, it turns out that the golden
 * ratio phi = (sqrt(5)-1)/2, or its negative, has particularly nice
 * properties.  (See Knuth vol 3, section 6.4, exercise 9.)
 *
 * These are the negative, (1 - phi) = phi**2 = (3 - sqrt(5))/2,
 * which is very slightly easier to multiply by and makes no
 * difference to the hash distribution.
 */
constexpr u32 GOLDEN_RATIO_32 = 0x61C88647;
constexpr u64 GOLDEN_RATIO_64 = 0x61C8864680B583EB;

/**
 * @brief Computes a 32-bit hash of a 32-bit value using multiplicative hashing.
 * @param val The 32-bit input value to hash.
 * @return A 32-bit hash value.
 */
[[gnu::const, gnu::always_inline]]
static inline u32 hash_32(u32 val, int bits)
{
	return (val * GOLDEN_RATIO_32) >> (32 - bits);
}

/**
 * @brief Computes a hash of a 64-bit value, scaled to a specified number of bits.
 * @param val  The 64-bit input value to hash.
 * @param bits The desired number of bits for the output hash value (1 to 64).
 * @return A hash value containing `bits` number of significant bits.
 */
[[gnu::const, gnu::always_inline]]
static inline u64 hash_64(u64 val, int bits)
{
	/* 64x64-bit multiply is efficient on all 64-bit processors */
	return (val * GOLDEN_RATIO_64) >> (64 - bits);
}

#define DEFINE_HASHTABLE(name, bits)                                         \
	struct hlist_head name[1 << (bits)] = { [0 ...((1 << (bits)) - 1)] = \
							HLIST_HEAD_INIT }

#define DECLARE_HASHTABLE(name, bits) struct hlist_head name[1 << (bits)]

#define HASH_SIZE(name) (ARRAY_SIZE(name))
#define HASH_BITS(name) ilog2((unsigned long)HASH_SIZE(name))

/* Use hash_32 when possible to allow for fast 32bit hashing in 64bit kernels. */
#define hash_min(val, bits) \
	(sizeof(val) <= 4 ? hash_32((u32)val, bits) : hash_64((u64)val, bits))

static inline void __hash_init(struct hlist_head* ht, unsigned int sz)
{
	unsigned int i;

	for (i = 0; i < sz; i++)
		INIT_HLIST_HEAD(&ht[i]);
}

/**
 * hash_init - initialize a hash table
 * @hashtable: hashtable to be initialized
 *
 * Calculates the size of the hashtable from the given parameter, otherwise
 * same as hash_init_size.
 *
 * This has to be a macro since HASH_BITS() will not work on pointers since
 * it calculates the size during preprocessing.
 */
#define hash_init(hashtable) __hash_init(hashtable, HASH_SIZE(hashtable))

/**
 * hash_add - add an object to a hashtable
 * @hashtable: hashtable to add to
 * @node: the &struct hlist_node of the object to be added
 * @key: the key of the object to be added
 */
#define hash_add(hashtable, node, key) \
	hlist_add_head(&hashtable[hash_min(key, HASH_BITS(hashtable))], node)

/**
 * hash_hashed - check whether an object is in any hashtable
 * @node: the &struct hlist_node of the object to be checked
 */
static inline bool hash_hashed(struct hlist_node* node)
{
	return !hlist_unhashed(node);
}

static inline bool __hash_empty(struct hlist_head* ht, unsigned int sz)
{
	unsigned int i;

	for (i = 0; i < sz; i++)
		if (!hlist_empty(&ht[i])) return false;

	return true;
}

/**
 * hash_empty - check whether a hashtable is empty
 * @hashtable: hashtable to check
 *
 * This has to be a macro since HASH_BITS() will not work on pointers since
 * it calculates the size during preprocessing.
 */
#define hash_empty(hashtable) __hash_empty(hashtable, HASH_SIZE(hashtable))

/**
 * hash_del - remove an object from a hashtable
 * @node: &struct hlist_node of the object to remove
 */
static inline void hash_del(struct hlist_node* node)
{
	hlist_del_init(node);
}

/**
 * hash_for_each - iterate over a hashtable
 * @name: hashtable to iterate
 * @bkt: integer to use as bucket loop cursor
 * @obj: the type * to use as a loop cursor for each entry
 * @member: the name of the hlist_node within the struct
 */
#define hash_for_each(name, bkt, obj, member)                               \
	for ((bkt) = 0, obj = NULL; obj == NULL && (bkt) < HASH_SIZE(name); \
	     (bkt)++)                                                       \
		hlist_for_each_entry (obj, &name[bkt], member)

/**
 * hash_for_each_possible - iterate over all possible objects hashing to the
 * same bucket
 * @name: hashtable to iterate
 * @obj: the type * to use as a loop cursor for each entry
 * @member: the name of the hlist_node within the struct
 * @key: the key of the objects to iterate over
 */
#define hash_for_each_possible(name, obj, member, key) \
	hlist_for_each_entry (                         \
		obj, &name[hash_min(key, HASH_BITS(name))], member)
