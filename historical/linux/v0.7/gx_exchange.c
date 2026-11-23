/* SPDX-License-Identifier: GPL-2.0-only */
#include "gx_offline.h"
#include <linux/string.h>
#include <linux/overflow.h>

static bool waiting(struct gx_exchange *x)
{
	return x->result.state == GX_WAIT_ACK || x->result.state == GX_WAIT_DATA;
}

static int finish(struct gx_exchange *x, enum gx_state state, int error)
{
	x->result.state = state;
	x->result.error = error;
	memset(x->result.version, 0, sizeof(x->result.version));
	return error;
}

void gx_exchange_init(struct gx_exchange *x)
{
	memset(x, 0, sizeof(*x));
	mutex_init(&x->lock);
}

int gx_exchange_begin(struct gx_exchange *x, u64 now, u64 ack_budget,
		      u64 data_budget, u64 *generation)
{
	u64 deadline;
	int ret = GX_OK;

	if (!generation || !ack_budget || !data_budget ||
	    check_add_overflow(now, ack_budget, &deadline))
		return GX_ARGUMENT;
	mutex_lock(&x->lock);
	if (x->result.state == GX_REMOVED)
		ret = GX_REMOVED_ERROR;
	else if (waiting(x))
		ret = GX_BUSY;
	else if (x->result.generation == U64_MAX)
		ret = GX_LIMIT;
	else {
		x->result.generation++;
		x->result.state = GX_WAIT_ACK;
		x->result.error = GX_OK;
		x->result.deadline = deadline;
		memset(x->result.version, 0, sizeof(x->result.version));
		x->data_budget = data_budget;
		*generation = x->result.generation;
	}
	mutex_unlock(&x->lock);
	return ret;
}

void gx_exchange_prepare(u64 gen, const u8 *p, size_t n, struct gx_prepared *r)
{
	struct gx_rx_staging staging;
	struct gx_packet packet;

	memset(r, 0, sizeof(*r));
	r->generation = gen;
	r->error = gx_rx_load(&staging, p, n);
	if (!r->error)
		r->error = gx_parse_packet(staging.bytes, staging.size, &packet);
	if (r->error)
		return;
	r->kind = packet.kind;
	if (packet.kind == GX_ACK) {
		r->ack_opcode = packet.payload[0];
		r->ack_status = packet.payload[1];
	} else {
		r->error = gx_copy_version(&packet, r->version, sizeof(r->version));
	}
}

int gx_exchange_commit(struct gx_exchange *x, const struct gx_prepared *r, u64 now)
{
	int ret;
	u64 deadline;

	mutex_lock(&x->lock);
	if (x->result.state == GX_REMOVED)
		ret = GX_REMOVED_ERROR;
	else if (r->generation != x->result.generation)
		ret = GX_STALE;
	else if (!waiting(x))
		ret = GX_STATE;
	else if (now >= x->result.deadline)
		ret = finish(x, GX_TIMED_OUT, GX_TIMEOUT);
	else if (r->error)
		ret = finish(x, GX_FAILED, r->error);
	else if (x->result.state == GX_WAIT_ACK) {
		if (r->kind != GX_ACK || r->ack_opcode != 0xa8)
			ret = finish(x, GX_FAILED, GX_UNEXPECTED);
		else if (r->ack_status != 1)
			ret = finish(x, GX_FAILED, GX_ACK_REJECTED);
		else if (check_add_overflow(now, x->data_budget, &deadline))
			ret = finish(x, GX_FAILED, GX_LIMIT);
		else {
			x->result.state = GX_WAIT_DATA;
			x->result.deadline = deadline;
			ret = GX_OK;
		}
	} else if (r->kind != GX_DATA_A8) {
		ret = finish(x, GX_FAILED, GX_UNEXPECTED);
	} else {
		memcpy(x->result.version, r->version, sizeof(r->version));
		x->result.state = GX_COMPLETE;
		ret = GX_OK;
	}
	mutex_unlock(&x->lock);
	return ret;
}

static int terminate(struct gx_exchange *x, u64 gen, enum gx_state state, int error)
{
	int ret;

	mutex_lock(&x->lock);
	if (x->result.state == GX_REMOVED)
		ret = GX_REMOVED_ERROR;
	else if (gen != x->result.generation)
		ret = GX_STALE;
	else if (!waiting(x))
		ret = GX_STATE;
	else
		ret = finish(x, state, error);
	mutex_unlock(&x->lock);
	return ret;
}

int gx_exchange_abort(struct gx_exchange *x, u64 gen, int error)
{
	return terminate(x, gen, GX_FAILED, error);
}

int gx_exchange_cancel(struct gx_exchange *x, u64 gen)
{
	return terminate(x, gen, GX_CANCELLED, GX_CANCELLED_ERROR);
}

int gx_exchange_expire(struct gx_exchange *x, u64 gen, u64 now)
{
	int ret;

	mutex_lock(&x->lock);
	if (x->result.state == GX_REMOVED)
		ret = GX_REMOVED_ERROR;
	else if (gen != x->result.generation)
		ret = GX_STALE;
	else if (!waiting(x))
		ret = GX_STATE;
	else if (now < x->result.deadline)
		ret = GX_BUSY;
	else
		ret = finish(x, GX_TIMED_OUT, GX_TIMEOUT);
	mutex_unlock(&x->lock);
	return ret;
}

void gx_exchange_remove(struct gx_exchange *x)
{
	mutex_lock(&x->lock);
	finish(x, GX_REMOVED, GX_REMOVED_ERROR);
	mutex_unlock(&x->lock);
}

void gx_exchange_snapshot(struct gx_exchange *x, struct gx_result *r)
{
	mutex_lock(&x->lock);
	*r = x->result;
	mutex_unlock(&x->lock);
}
