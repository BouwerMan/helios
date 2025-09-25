#include <uapi/helios/errno.h>

#include "drivers/serial.h"
#include "kernel/assert.h"
#include "kernel/atomic.h"
#include "kernel/bitops.h"
#include "kernel/klog.h"
#include "kernel/kmath.h"
#include "kernel/softirq.h"
#include "kernel/timer.h"
#include "kernel/types.h"
#include "mm/page_alloc.h"

// TODO: Implement backlog tracking that effects delay

static atomic64_t klog_dropped_records = ATOMIC64_INIT(0);

struct klog_ring g_klog_ring = { 0 };
struct klog_cursor g_klog_cursor = { 0 };

static bool klog_is_empty(const struct klog_ring* rb,
			  const struct klog_cursor* cur)
{
	u64 head = (u64)atomic64_load_acquire(&rb->head_bytes);
	return cur->bytes == head;
}

static int klog_emit_serial(const struct klog_header* hdr,
			    const u8* payload,
			    u32 payload_len,
			    void* cookie)
{
	(void)hdr;
	(void)cookie;
	write_serial_n((const char*)payload, payload_len);
	return 0;
}

static softirq_ret_t klog_softirq_action(size_t item_budget, u64 ns_budget)
{
	static constexpr size_t CHUNK_MIN = 1;
	static constexpr size_t CHUNK_MAX = 64;
	size_t chunk = 16;
	u64 deadline = clock_now_ns() + ns_budget;

	while (item_budget > 0) {
		u64 now = clock_now_ns();
		if (now >= deadline) {
			// Out of time
			return klog_is_empty(&g_klog_ring, &g_klog_cursor) ?
				       SOFTIRQ_DONE :
				       SOFTIRQ_MORE;
		}

		size_t n = item_budget < chunk ? item_budget : chunk;

		int res = klog_drain(&g_klog_ring,
				     &g_klog_cursor,
				     klog_emit_serial,
				     nullptr,
				     (u32)n);

		switch ((enum KLOG_DRAIN_STATUS)res) {
		case KLOG_DRAIN_BUDGET_EXHAUSTED:
			// Emitted 'n' records
			item_budget -= n;
			break;

		case KLOG_DRAIN_OK:
			// Drained to current head snapshot. If empty now, we're done.
			if (klog_is_empty(&g_klog_ring, &g_klog_cursor)) {
				return SOFTIRQ_DONE;
			}
			// New records arrived; don't charge full chunk since we don't know how many.
			if (item_budget) {
				item_budget--; // conservative accounting
			}
			continue;

		case KLOG_DRAIN_RESYNCED:
			// Nothing to flush after resync
			return SOFTIRQ_DONE;

		case KLOG_DRAIN_STOPPED_UNCOMMITTED:
			// We hit an in-flight record: stop this pass, try again later
			return SOFTIRQ_MORE;

		case KLOG_DRAIN_EMIT_BACKPRESSURE:
			// TODO:This will matter one day, it should depend on
			// how a sink becomes "ready". For now, UART literally
			// never exerts backpressure.
			kassert(false, "Unexpected backpressure");
			__builtin_unreachable();
		}

		// adapt chunk by how close we are to the deadline
		now = clock_now_ns();
		if (deadline - now <= ns_budget / 8) {
			// in the last ~12.5% of time: drain in singles
			chunk = CHUNK_MIN;
		} else if (chunk < CHUNK_MAX) {
			// plenty of time: ramp up a bit for throughput
			chunk *= 2;
			if (chunk > CHUNK_MAX) chunk = CHUNK_MAX;
		}
	}

	return klog_is_empty(&g_klog_ring, &g_klog_cursor) ? SOFTIRQ_DONE :
							     SOFTIRQ_MORE;
}

struct klog_ring* klog_init()
{
	void* buf = get_free_pages(AF_KERNEL, KLOG_SIZE_PAGES);
	int res = klog_ring_init(&g_klog_ring, buf, KLOG_SIZE_BYTES);
	if (res < 0) {
		panic("klog_ring_init failed");
	}

	g_klog_cursor.bytes =
		(u64)atomic64_load_relaxed(&g_klog_ring.head_bytes);
	g_klog_cursor.last_seq =
		(u64)atomic64_load_relaxed(&g_klog_ring.next_seq);

	softirq_register(SOFTIRQ_KLOG, "klog", klog_softirq_action);

	return &g_klog_ring;
}

int klog_ring_init(struct klog_ring* rb, void* buf, u32 size_pow2)
{
	bool buf_valid = buf && CHECK_ALIGN(buf, 8);
	bool size_valid = is_pow_of_two(size_pow2);
	if (!rb || !buf_valid || !size_valid) {
		return -EINVAL;
	}

	rb->buf = buf;
	rb->size = size_pow2;
	rb->mask = size_pow2 - 1;

	rb->head_bytes = (atomic64_t) { 0 };
	rb->next_seq = (atomic64_t) { 0 };

	memset(rb->buf, 0, size_pow2);

	return 0;
}

/**
 * emit_padding - publish a PADDING record that fills the tail to end-of-ring
 * @rb:   initialized ring
 * @pos:  masked byte offset within the ring buffer (0 <= pos < rb->size)
 * @len:  number of bytes from @pos to end-of-ring (strictly > 0, 8B-aligned)
 */
static inline void emit_padding(struct klog_ring* rb, u32 pos, u32 len)
{
	kassert(rb && pos + len <= rb->size, "Invalid args to emit_padding");
	kassert(len > 0 && (len % 8) == 0,
		"len must be positive and 8-byte aligned");

	struct klog_header* hdr = (struct klog_header*)&rb->buf[pos];
	u32 sf = klog_make_sf(len, KFLAG_PADDING | KFLAG_COMMITTED);

	smp_store_release_u32(&hdr->size_flags, sf);
}

/**
 * klog_reserve_bytes - reserve a contiguous byte range for one record
 * @rb:    initialized ring
 * @len:   total bytes for this record (header + payload + pad), 8B-aligned
 * @start: OUT: unbounded starting byte index (monotonic head before add)
 * @off:   OUT: masked in-buffer offset (== *start & rb->mask)
 *
 * Returns: true on success (always, under current algorithm).
 *
 * Purpose:
 *   Multi-producer, lock-free reservation of exactly @len contiguous bytes.
 *   If there is not enough room at the tail to fit @len, the function first
 *   advances the head by the leftover tail and publishes a PADDING record
 *   at @off, then retries; the actual record will land at offset 0.
 *
 * Algorithm (CAS loop):
 *   1) Snapshot head := atomic64_load_relaxed().
 *   2) Compute pos = head & mask; tail = size - pos.
 *   3) If (tail >= len):
 *        try CAS(head -> head+len). On success, you own [pos, pos+len).
 *      Else if (tail > 0):
 *        try CAS(head -> head+tail). On success, emit_padding(pos, tail) and retry.
 *      Else (tail == 0):
 *        exactly at boundary; retry.
 *
 * Concurrency & ordering:
 *   - CAS is RELAXED: allocation does not publish data.
 *   - PADDING is published with a RELEASE store to size_flags so readers can
 *     skip it safely with ACQUIRE loads.
 *   - Progress is lock-free: on contention, at least one producer succeeds.
 *
 * Preconditions (enforced by assertions):
 *   - rb, start, off are non-null.
 *   - len is 8-byte aligned, len >= sizeof(struct klog_header), len <= rb->size.
 *
 * Postconditions on success:
 *   - *start is the unbounded byte index previously held in head.
 *   - *off   is the masked in-buffer offset of the reservation.
 *   - The reserved region [*off, *off+len) does not straddle end-of-ring.
 */
bool klog_reserve_bytes(struct klog_ring* rb, u32 len, u64* start, u32* off)
{
	kassert(rb && start && off, "Invalid args to klog_reserve_bytes");
	kassert(len % 8 == 0, "len must be 8-byte aligned");
	kassert(len <= rb->size, "len too large to fit in ring buffer");
	kassert(sizeof(struct klog_header) <= len, "len too small for header");

	while (true) {
		long head = (long)atomic64_load_relaxed(&rb->head_bytes);
		u64 pos = (u64)head & rb->mask;
		u64 tail = rb->size - pos;

		if (tail >= len) {
			if (a64_cas_relaxed(&rb->head_bytes,
					    &head,
					    head + len)) {
				*start = (u64)head;
				*off = (u32)pos;
				return true;
			}

			// Lost race, retry
			cpu_relax();
			continue;
		}

		if (tail > 0) {
			if (a64_cas_relaxed(&rb->head_bytes,
					    &head,
					    head + tail)) {
				emit_padding(rb, (u32)pos, (u32)tail);

				// Now try to reserve real record
				continue;
			}

			cpu_relax();
			continue;
		}

		// tail == 0
		continue;
	}
}

void klog_fill_and_publish(struct klog_ring* rb,
			   u32 off,
			   u32 total,
			   klog_level_t level,
			   const char* msg,
			   u32 msg_len,
			   u64 seq)
{
	kassert(rb && msg, "Invalid args to klog_fill_and_publish");
	kassert(total > 0 && (total % 8) == 0,
		"total must be positive and 8-byte aligned");
	kassert(msg_len <= total - (KLOG_HDR_LEN_8 * 8),
		"msg_len exceeds payload");

	struct klog_header* hdr = (struct klog_header*)&rb->buf[off];
	hdr->magic = KLOG_MAGIC;
	hdr->version = KLOG_VERSION;
	hdr->hdr_len_8 = KLOG_HDR_LEN_8;
	hdr->seq = seq;
	hdr->tsc = clock_now_ns();
	// TODO: per cpu ID
	hdr->id = klog_pack_id(level, 0, 0);

	memcpy(((u8*)hdr + (size_t)KLOG_HDR_LEN_8 * 8), msg, msg_len);

	hdr->payload_len = msg_len;

	smp_store_release_u32(&hdr->size_flags, klog_sf_committed(total));
}

bool klog_try_write(struct klog_ring* rb,
		    klog_level_t level,
		    const char* msg,
		    u32 msg_len,
		    u64* out_seq)
{
	kassert(rb && msg, "Invalid args to klog_try_write");
	const u32 hdr = KLOG_HDR_LEN_8 * 8;
	const u32 max_payload = rb->size - hdr;
	const u32 cap = MIN(max_payload, rb->size >> 2); // 25% cap
	if (msg_len > cap) msg_len = cap;

	const u32 total = ALIGN_UP(hdr + msg_len, 8);

	if (total > rb->size) {
		// Message too large to fit in ring buffer
		atomic64_fetch_add_relaxed(&klog_dropped_records, 1);
		return false;
	}

	u64 start;
	u32 off;

	if (!klog_reserve_bytes(rb, total, &start, &off)) {
		atomic64_fetch_add_relaxed(&klog_dropped_records, 1);
		return false;
	}

	u64 seq = (u64)atomic64_fetch_add_relaxed(&rb->next_seq, 1);

	klog_fill_and_publish(rb, off, total, level, msg, msg_len, seq);
	if (out_seq) *out_seq = seq;

	softirq_raise(SOFTIRQ_KLOG);

	return true;
}

void klog_flush()
{
	klog_drain(&g_klog_ring,
		   &g_klog_cursor,
		   klog_emit_serial,
		   nullptr,
		   UINT32_MAX);
}

static constexpr u32 REFRESH_HEAD_EVERY = 64;
_Static_assert((REFRESH_HEAD_EVERY & (REFRESH_HEAD_EVERY - 1)) == 0,
	       "REFRESH_HEAD_EVERY must be a power of 2");

int klog_drain(struct klog_ring* rb,
	       struct klog_cursor* cur,
	       klog_emit_fn emit,
	       void* cookie,
	       u32 budget_records)
{
	if (!emit || !rb || !cur || budget_records == 0) {
		return -EINVAL;
	}

	u64 head_snapshot = (u64)atomic64_load_relaxed(&rb->head_bytes);

	u64 oldest = (head_snapshot >= rb->size) ? (head_snapshot - rb->size) :
						   0;

	bool lost_records = false;

	if (cur->bytes < oldest) {
		//Fell behind
		u64 new_pos = klog_resync_scan(rb, oldest, head_snapshot);
		if (new_pos == head_snapshot) {
			// Nothing to catch up to
			cur->bytes = head_snapshot;
			return KLOG_DRAIN_RESYNCED;
		}
		cur->bytes = new_pos;
		lost_records = true;
	}

	while (budget_records > 0 && cur->bytes < head_snapshot) {
		if ((budget_records & (REFRESH_HEAD_EVERY - 1)) == 0) {
			// Periodically refresh head snapshot to avoid spinning too long
			head_snapshot =
				(u64)atomic64_load_relaxed(&rb->head_bytes);
			if (cur->bytes >= head_snapshot) {
				break;
			}
		}

		u32 off = cur->bytes & rb->mask;
		struct klog_header* hdr = (struct klog_header*)&rb->buf[off];

		u32 sf = smp_load_acquire_u32(&hdr->size_flags);
		if (!klog_is_committed(sf)) {
			return KLOG_DRAIN_STOPPED_UNCOMMITTED;
		}

		u64 len_total = klog_len_from_sf(sf);
		if (len_total < (u64)KLOG_HDR_LEN_8 * 8 ||
		    (len_total % 8) != 0 || len_total > rb->size) {
			cur->bytes += 8;
			if (cur->bytes >= head_snapshot) {
				return KLOG_DRAIN_OK;
			}
			continue;
		}

		if (klog_is_padding(sf)) {
			// Skip padding
			cur->bytes += len_total;
			continue;
		}

		size_t hdr_bytes = (size_t)hdr->hdr_len_8 * 8;

		// Keep track of how may records we lost due to being too slow
		if (lost_records && hdr->seq > cur->last_seq) {
			lost_records = false;
			cur->lost += hdr->seq - (cur->last_seq + 1);
		}

		if (hdr->hdr_len_8 < 4 || hdr->magic != KLOG_MAGIC ||
		    len_total < hdr_bytes ||
		    hdr->payload_len > len_total - hdr_bytes) {
			// Invalid header, skip 8 bytes and try again
			cur->bytes += 8;
			if (cur->bytes >= head_snapshot) {
				return KLOG_DRAIN_OK;
			}
			continue;
		}

		u32 payload_len =
			MIN(hdr->payload_len, (u32)len_total - (u32)hdr_bytes);

		kassert(hdr->payload_len <= len_total - hdr_bytes,
			"payload_len sanity");

		kassert(len_total % 8 == 0 && len_total <= rb->size &&
				len_total >= hdr_bytes,
			"len_total sanity");

		int res = emit(hdr, (u8*)hdr + hdr_bytes, payload_len, cookie);
		cur->bytes += len_total;
		cur->last_seq = hdr->seq;
		if (res < 0) {
			return KLOG_DRAIN_EMIT_BACKPRESSURE;
		}

		budget_records--;
	}

	return budget_records == 0 ? KLOG_DRAIN_BUDGET_EXHAUSTED :
				     KLOG_DRAIN_OK;
}

/* Helper used inside drain when we detect an overrun */
u64 klog_resync_scan(const struct klog_ring* rb,
		     u64 scan_from,
		     u64 head_snapshot)
{
	u64 scan_pos = ALIGN_UP(scan_from, 8);
	for (; scan_pos < head_snapshot; scan_pos += 8) {
		u32 off = (u32)(scan_pos & rb->mask);
		struct klog_header* hdr = (struct klog_header*)&rb->buf[off];

		u32 sf = smp_load_acquire_u32(&hdr->size_flags);
		if (!klog_is_committed(sf)) {
			return head_snapshot;
		}

		u64 len = klog_len_from_sf(sf);
		if (len < (u64)KLOG_HDR_LEN_8 * 8 || (len % 8) != 0 ||
		    len > rb->size) {
			continue;
		}

		if (klog_is_padding(sf)) {
			return scan_pos;
		}

		if (hdr->hdr_len_8 < 4 || hdr->magic != KLOG_MAGIC ||
		    len < (u64)hdr->hdr_len_8 * 8 ||
		    hdr->payload_len > len - (u32)(hdr->hdr_len_8 * 8)) {
			continue;
		}

		return scan_pos;
	}

	return head_snapshot;
}
