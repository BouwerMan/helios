/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include <stdint.h>

// Unsigned types
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef unsigned char uchar;
typedef unsigned int uint;
typedef unsigned long ulong;

// Signed types
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

// Pointer-width types
typedef uintptr_t uptr;
typedef intptr_t iptr;

// Flags/bitmask type
typedef unsigned long flags_t;

typedef unsigned long size_t;
typedef long ssize_t;
typedef long off_t;

typedef unsigned long paddr_t;
typedef unsigned long vaddr_t;

typedef int pid_t;

typedef struct {
	int counter;
} atomic_t;

typedef struct {
	long counter;
} atomic64_t;

struct list_head {
	struct list_head *next, *prev;
};

struct hlist_head {
	struct hlist_node* first;
};

struct hlist_node {
	struct hlist_node *next, **pprev;
};
