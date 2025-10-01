/* SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once

#include "arch/cache.h"
#include "kernel/compiler_attributes.h"
#include "kernel/timer.h"
#include "kernel/types.h"
#include "mm/page.h"

typedef enum __klog_level {
	KLOG_EMERG = 0,	  // System is unusable
	KLOG_ALERT = 1,	  // Action must be taken immediately
	KLOG_CRIT = 2,	  // Critical conditions
	KLOG_ERR = 3,	  // Error conditions
	KLOG_WARNING = 4, // Warning conditions
	KLOG_NOTICE = 5,  // Normal but significant condition
	KLOG_INFO = 6,	  // Informational
	KLOG_DEBUG = 7	  // Debug-level messages
} klog_level_t;

static constexpr size_t KLOG_ORDER = 8;
static constexpr size_t KLOG_SIZE_PAGES = (1UL << KLOG_ORDER);
static constexpr size_t KLOG_SIZE_BYTES = KLOG_SIZE_PAGES * PAGE_SIZE;

static constexpr u32 KFLAG_COMMITTED = 1U << 31;
static constexpr u32 KFLAG_PADDING = 1U << 30;
static constexpr u32 KFLAG_SYNTHETIC = 1U << 29;
static constexpr u32 KFLAG_SIZE_MASK = (1U << 29) - 1;

static constexpr u16 KLOG_MAGIC = 0x484C;
static constexpr u8 KLOG_VERSION = 1;

struct klog_header {
	u32 size_flags;
	u16 magic;
	u8 version;
	u8 hdr_len_8;
	u64 seq;
	u64 tsc;
	u32 id;
	u32 payload_len;
};

static constexpr u8 KLOG_HDR_LEN_8 = 32 / 8;
_Static_assert(KLOG_HDR_LEN_8 == 4, "log_header must be 32 bytes");
_Static_assert(sizeof(struct klog_header) == (size_t)(KLOG_HDR_LEN_8 * 8U),
	       "log_header must be 32 bytes");

struct klog_ring {
	u8* buf;
	u32 size; // Power of 2
	u32 mask; // size - 1

	// Hot parameters, written by producers:
	atomic64_t head_bytes; // Next free byte
	atomic64_t next_seq;   // Next sequence number
} __aligned(L1_CACHE_SIZE);

struct klog_cursor {
	u64 bytes;    // unbounded byte offset to next record
	u64 last_seq; // last sequence consumed (0 if none)
	u64 lost;     // records skipped due to overrun
	struct timer timer;
};

typedef int (*klog_emit_fn)(const struct klog_header* hdr,
			    const u8* payload,
			    u32 payload_len,
			    void* cookie /* sink context */
);

/**
 * enum KLOG_DRAIN_STATUS - return codes from klog_drain()
 * @KLOG_DRAIN_OK: drained all available records
 * @KLOG_DRAIN_STOPPED_UNCOMMITTED: hit a not-yet-published record
 * @KLOG_DRAIN_BUDGET_EXHAUSTED: emitted 'budget' records
 * @KLOG_DRAIN_EMIT_BACKPRESSURE: sink asked us to stop (nonzero return)
 * @KLOG_DRAIN_RESYNCED: overrun detected; cursor jumped forward
 */
enum KLOG_DRAIN_STATUS {
	KLOG_DRAIN_OK = 0,
	KLOG_DRAIN_STOPPED_UNCOMMITTED,
	KLOG_DRAIN_BUDGET_EXHAUSTED,
	KLOG_DRAIN_EMIT_BACKPRESSURE,
	KLOG_DRAIN_RESYNCED,
};

// Going to wait until things are fully setup to init this.
// We will just straight log to serial or screen until the buddy allocator is available.
struct klog_ring* klog_init();

int klog_ring_init(struct klog_ring* rb, void* buf, u32 size_pow2);

bool klog_try_write(struct klog_ring* rb,
		    klog_level_t level,
		    const char* msg,
		    u32 msg_len,
		    u64* out_seq /* optional */);

void klog_flush();
