/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_OFFLINE_H
#define GX_OFFLINE_H
#include "gx_protocol_core.h"
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/completion.h>
#include <linux/atomic.h>

struct gx_rx_staging {
	u8 bytes[GX_MAX_PACKET];
	struct gx_header header;
	size_t size;
	bool has_header, ready;
};
void gx_rx_reset(struct gx_rx_staging *s);
int gx_rx_header(struct gx_rx_staging *s, const u8 *p, size_t n);
int gx_rx_body(struct gx_rx_staging *s, const u8 *p, size_t n);
int gx_rx_load(struct gx_rx_staging *s, const u8 *p, size_t n);

enum gx_state { GX_IDLE, GX_WAIT_ACK, GX_WAIT_DATA, GX_COMPLETE,
	GX_FAILED, GX_TIMED_OUT, GX_CANCELLED, GX_REMOVED };
struct gx_result {
	enum gx_state state;
	u64 generation, deadline;
	int error;
	char version[GX_MAX_VERSION];
};
struct gx_exchange {
	struct mutex lock;
	struct gx_result result;
	u64 data_budget;
};
/* Prepared data owns all bytes; parsing occurs without the exchange mutex. */
struct gx_prepared {
	u64 generation;
	int error;
	enum gx_kind kind;
	u8 ack_opcode, ack_status;
	char version[GX_MAX_VERSION];
};
void gx_exchange_init(struct gx_exchange *x);
int gx_exchange_begin(struct gx_exchange *x, u64 now, u64 ack_budget,
		      u64 data_budget, u64 *generation);
void gx_exchange_prepare(u64 gen, const u8 *p, size_t n,
			 struct gx_prepared *r);
int gx_exchange_commit(struct gx_exchange *x, const struct gx_prepared *r, u64 now);
int gx_exchange_abort(struct gx_exchange *x, u64 gen, int error);
int gx_exchange_cancel(struct gx_exchange *x, u64 gen);
int gx_exchange_expire(struct gx_exchange *x, u64 gen, u64 now);
void gx_exchange_remove(struct gx_exchange *x);
void gx_exchange_snapshot(struct gx_exchange *x, struct gx_result *r);
int gx_transport_submit(void);

#define GX_QUEUE_DEPTH 8
struct gx_test_event {
	u64 generation, now;
	size_t size;
	bool timeout;
	u8 bytes[GX_MAX_PACKET];
};
struct gx_test_event_source;
struct gx_test_worker {
	struct work_struct work;
	struct gx_test_event_source *source;
};
struct gx_test_event_source {
	spinlock_t queue_lock;
	/* dispatch may enclose queue_lock (pop) or exchange.lock (commit).
	 * Never acquire a mutex while holding queue_lock; exchange never nests
	 * dispatch/queue_lock. Producers release queue_lock before aborting.
	 */
	struct mutex dispatch;
	struct workqueue_struct *wq;
	struct gx_test_worker workers[2];
	struct gx_exchange *exchange;
	struct gx_test_event queue[GX_QUEUE_DEPTH];
	unsigned int head, count;
	bool removed, overflow;
	u64 overflow_gen;
	atomic_t active_workers, peak_workers, processed;
	/* Test-only pause after prepare, before commit, while holding dispatch. */
	atomic_t pause_next;
	struct completion prepared, release, concurrent;
};
int gx_test_source_init(struct gx_test_event_source *s, struct gx_exchange *x);
/* Process-context synthetic producers only, NOT physical IRQ callbacks.
 * Caller owns source/exchange until all producers and remove have returned.
 * Input is copied before return. Never pass a device or transport callback.
 */
int gx_test_source_packet(struct gx_test_event_source *s, u64 gen, u64 now,
			  const u8 *p, size_t n);
int gx_test_source_timeout(struct gx_test_event_source *s, u64 gen, u64 now);
void gx_test_source_flush(struct gx_test_event_source *s);
void gx_test_source_pause(struct gx_test_event_source *s);
/* One owner serializes pause/flush/remove; remove blocks until both workers
 * have stopped. Packet/timeout producers may race remove, but must return
 * before the owner frees this source or exchange. No refcounted public handle.
 */
void gx_test_source_remove(struct gx_test_event_source *s);
#endif
