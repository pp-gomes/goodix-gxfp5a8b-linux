/* SPDX-License-Identifier: GPL-2.0-only */
#include "gx_offline.h"
#include <linux/string.h>
#include <linux/errno.h>

static bool pop(struct gx_test_event_source *s, struct gx_test_event *e)
{
	unsigned long flags;
	bool found = false;

	spin_lock_irqsave(&s->queue_lock, flags);
	if (!s->removed && s->overflow) {
		memset(e, 0, sizeof(*e));
		e->generation = s->overflow_gen;
		/* size > maximum is an internal overflow marker, never copied. */
		e->size = GX_MAX_PACKET + 1;
		s->overflow = false;
		found = true;
	} else if (!s->removed && s->count) {
		*e = s->queue[s->head];
		memset(&s->queue[s->head], 0, sizeof(*e));
		s->head = (s->head + 1) % GX_QUEUE_DEPTH;
		s->count--;
		found = true;
	}
	spin_unlock_irqrestore(&s->queue_lock, flags);
	return found;
}

static void worker(struct work_struct *work)
{
	struct gx_test_worker *w = container_of(work, struct gx_test_worker, work);
	struct gx_test_event_source *s = w->source;
	struct gx_test_event e;
	struct gx_prepared r;
	int active = atomic_inc_return(&s->active_workers);

	atomic_cmpxchg(&s->peak_workers, 0, 1);
	if (active == 2) {
		atomic_set(&s->peak_workers, 2);
		complete(&s->concurrent);
	}
	/* Serializes dequeue and commit; two workers cannot reorder FIFO packets. */
	mutex_lock(&s->dispatch);
	while (pop(s, &e)) {
		if (e.size > GX_MAX_PACKET) {
			gx_exchange_abort(s->exchange, e.generation, GX_OVERFLOW);
		} else if (e.timeout) {
			gx_exchange_expire(s->exchange, e.generation, e.now);
		} else {
			gx_exchange_prepare(e.generation, e.bytes, e.size, &r);
			if (atomic_xchg(&s->pause_next, 0)) {
				complete(&s->prepared);
				wait_for_completion(&s->release);
			}
			gx_exchange_commit(s->exchange, &r, e.now);
		}
		atomic_inc(&s->processed);
	}
	mutex_unlock(&s->dispatch);
	atomic_dec(&s->active_workers);
}

int gx_test_source_init(struct gx_test_event_source *s, struct gx_exchange *x)
{
	int i;

	memset(s, 0, sizeof(*s));
	spin_lock_init(&s->queue_lock);
	mutex_init(&s->dispatch);
	init_completion(&s->prepared);
	init_completion(&s->release);
	init_completion(&s->concurrent);
	atomic_set(&s->active_workers, 0);
	atomic_set(&s->peak_workers, 0);
	atomic_set(&s->processed, 0);
	atomic_set(&s->pause_next, 0);
	s->exchange = x;
	for (i = 0; i < 2; i++) {
		s->workers[i].source = s;
		INIT_WORK(&s->workers[i].work, worker);
	}
	s->wq = alloc_workqueue("gx_offline", WQ_UNBOUND | WQ_MEM_RECLAIM, 2);
	return s->wq ? 0 : -ENOMEM;
}

static int enqueue(struct gx_test_event_source *s, u64 gen, u64 now,
		   const u8 *p, size_t n, bool timeout)
{
	unsigned long flags;
	struct gx_test_event *e;
	int ret = GX_OK, i;

	if ((!p && !timeout) || n > GX_MAX_PACKET)
		return n > GX_MAX_PACKET ? GX_LIMIT : GX_ARGUMENT;
	spin_lock_irqsave(&s->queue_lock, flags);
	if (s->removed) {
		ret = GX_REMOVED_ERROR;
	} else {
		if (s->count == GX_QUEUE_DEPTH || s->overflow) {
			if (!s->overflow) {
				s->overflow = true;
				s->overflow_gen = gen;
			}
			ret = GX_OVERFLOW;
		} else {
			e = &s->queue[(s->head + s->count) % GX_QUEUE_DEPTH];
			memset(e, 0, sizeof(*e));
			e->generation = gen;
			e->now = now;
			e->size = n;
			e->timeout = timeout;
			if (n)
				memcpy(e->bytes, p, n);
			s->count++;
		}
		/* Queue under spinlock so remove cannot destroy wq before scheduling. */
		for (i = 0; i < 2; i++)
			queue_work(s->wq, &s->workers[i].work);
	}
	spin_unlock_irqrestore(&s->queue_lock, flags);
	/* Producers are process-context only. Also fail a newly dropped generation
	 * when a prior generation already owns the pending overflow marker.
	 */
	if (ret == GX_OVERFLOW)
		gx_exchange_abort(s->exchange, gen, GX_OVERFLOW);
	return ret;
}

int gx_test_source_packet(struct gx_test_event_source *s, u64 gen, u64 now,
			  const u8 *p, size_t n)
{
	return enqueue(s, gen, now, p, n, false);
}

int gx_test_source_timeout(struct gx_test_event_source *s, u64 gen, u64 now)
{
	return enqueue(s, gen, now, NULL, 0, true);
}

void gx_test_source_flush(struct gx_test_event_source *s)
{
	flush_workqueue(s->wq);
}

void gx_test_source_pause(struct gx_test_event_source *s)
{
	/* Owner must flush first; no concurrent rearming. */
	reinit_completion(&s->prepared);
	reinit_completion(&s->release);
	atomic_set(&s->pause_next, 1);
}

void gx_test_source_remove(struct gx_test_event_source *s)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&s->queue_lock, flags);
	s->removed = true;
	s->count = 0;
	s->overflow = false;
	memset(s->queue, 0, sizeof(s->queue));
	spin_unlock_irqrestore(&s->queue_lock, flags);
	gx_exchange_remove(s->exchange);
	complete_all(&s->release);
	for (i = 0; i < 2; i++)
		cancel_work_sync(&s->workers[i].work);
	destroy_workqueue(s->wq);
	s->wq = NULL;
}
