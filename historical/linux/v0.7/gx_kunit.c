/* SPDX-License-Identifier: GPL-2.0-only */
#include <kunit/test.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include "gx_offline.h"
#include "tests/fixtures.h"

struct gx_fixture {
	struct gx_exchange x;
	struct gx_test_event_source s;
};

static int setup(struct kunit *test)
{
	struct gx_fixture *f = kunit_kzalloc(test, sizeof(*f), GFP_KERNEL);

	if (!f)
		return -ENOMEM;
	test->priv = f;
	gx_exchange_init(&f->x);
	return gx_test_source_init(&f->s, &f->x);
}

static void teardown(struct kunit *test)
{
	struct gx_fixture *f = test->priv;

	if (f->s.wq)
		gx_test_source_remove(&f->s);
}

static u64 begin(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = 0;

	KUNIT_EXPECT_EQ(test, gx_exchange_begin(&f->x, 0, 100, 100, &gen), GX_OK);
	return gen;
}

static void state(struct kunit *test, enum gx_state expected, int error)
{
	struct gx_fixture *f = test->priv;
	struct gx_result r;

	gx_exchange_snapshot(&f->x, &r);
	KUNIT_EXPECT_EQ(test, r.state, expected);
	KUNIT_EXPECT_EQ(test, r.error, error);
	if (expected != GX_COMPLETE)
		KUNIT_EXPECT_EQ(test, r.version[0], '\0');
	else
		KUNIT_EXPECT_STREQ(test, r.version, FX_VERSION);
}

static void deliver(struct kunit *test, u64 gen, const u8 *p, size_t n, u64 now)
{
	struct gx_fixture *f = test->priv;

	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, now, p, n), GX_OK);
	gx_test_source_flush(&f->s);
}

static void repair(u8 *p, size_t n)
{
	u8 sum = 0;
	size_t i;

	p[3] = p[0] + p[1] + p[2];
	for (i = 4; i + 1 < n; i++)
		sum += p[i];
	p[n - 1] = 0xaa - sum;
}

static void real_protocol(struct kunit *test)
{
	struct gx_packet packet;
	struct gx_header header;
	u8 out[GX_A8_SPI_SIZE];
	u16 cpu;
	size_t written;
	char version[GX_MAX_VERSION];

	KUNIT_ASSERT_EQ(test, gx_build_a8_spi(out, sizeof(out), &written), GX_OK);
	KUNIT_EXPECT_EQ(test, written, sizeof(fx_request));
	KUNIT_EXPECT_EQ(test, memcmp(out, fx_request, sizeof(out)), 0);
	KUNIT_EXPECT_EQ(test, gx_validate_a8_spi(out, sizeof(out)), GX_OK);
	KUNIT_ASSERT_EQ(test, gx_parse_header(fx_ack, 4, &header), GX_OK);
	KUNIT_EXPECT_EQ(test, header.body_size, (size_t)6);
	KUNIT_ASSERT_EQ(test, gx_parse_packet(fx_ack, sizeof(fx_ack), &packet), GX_OK);
	KUNIT_EXPECT_EQ(test, packet.kind, GX_ACK);
	KUNIT_ASSERT_EQ(test, gx_parse_packet(fx_data, sizeof(fx_data), &packet), GX_OK);
	KUNIT_ASSERT_EQ(test, gx_copy_version(&packet, version, sizeof(version)), GX_OK);
	KUNIT_EXPECT_STREQ(test, version, FX_VERSION);
	KUNIT_ASSERT_EQ(test, gx_decode_cpu(fx_cpu, sizeof(fx_cpu), &cpu), GX_OK);
	KUNIT_EXPECT_EQ(test, cpu, (u16)0x00a1);
	KUNIT_EXPECT_EQ(test, gx_transport_submit(), GX_HARDWARE_DISABLED);
}

static void malformed_protocol(struct kunit *test)
{
	struct gx_packet packet;
	struct gx_header header;
	u8 p[sizeof(fx_data)], request[GX_A8_SPI_SIZE];
	u8 oversized[] = { 0xa0, 1, 1, 0xa2 };
	size_t i, bit, written = 99;

	for (i = 0; i < sizeof(fx_data); i++)
		KUNIT_EXPECT_NE(test, gx_parse_packet(fx_data, i, &packet), GX_OK);
	for (i = 0; i < sizeof(fx_ack); i++)
		KUNIT_EXPECT_NE(test, gx_parse_packet(fx_ack, i, &packet), GX_OK);
	for (i = 0; i < sizeof(fx_request); i++)
		KUNIT_EXPECT_NE(test, gx_validate_a8_spi(fx_request, i), GX_OK);
	for (i = 0; i < sizeof(fx_data); i++) {
		for (bit = 0; bit < 8; bit++) {
			memcpy(p, fx_data, sizeof(p));
			p[i] ^= BIT(bit);
			KUNIT_EXPECT_NE(test, gx_parse_packet(p, sizeof(p), &packet), GX_OK);
		}
	}
	for (i = 0; i < sizeof(fx_request); i++) {
		memcpy(request, fx_request, sizeof(request));
		request[i] ^= 1;
		KUNIT_EXPECT_NE(test, gx_validate_a8_spi(request, sizeof(request)), GX_OK);
	}
	KUNIT_EXPECT_EQ(test, gx_parse_header(oversized, 4, &header), GX_LIMIT);
	memcpy(p, fx_data, sizeof(p));
	p[5]--;
	repair(p, sizeof(p));
	KUNIT_EXPECT_EQ(test, gx_parse_packet(p, sizeof(p), &packet), GX_LENGTH);
	p[5]++;
	p[4] = 0xc0;
	repair(p, sizeof(p));
	KUNIT_EXPECT_EQ(test, gx_parse_packet(p, sizeof(p), &packet), GX_UNEXPECTED);
	KUNIT_EXPECT_EQ(test, gx_build_a8_spi(request, sizeof(request) - 1, &written), GX_BUFFER);
	KUNIT_EXPECT_EQ(test, written, (size_t)0);
	KUNIT_EXPECT_EQ(test, gx_parse_packet(NULL, 0, &packet), GX_ARGUMENT);
}

static void staging_lifetime(struct kunit *test)
{
	struct gx_rx_staging staging;
	struct gx_packet packet;
	u8 header[4], body[sizeof(fx_data) - 4];
	char version[GX_MAX_VERSION];

	gx_rx_reset(&staging);
	memcpy(header, fx_data, sizeof(header));
	memcpy(body, fx_data + 4, sizeof(body));
	KUNIT_EXPECT_EQ(test, gx_rx_body(&staging, body, sizeof(body)), GX_STATE);
	KUNIT_ASSERT_EQ(test, gx_rx_header(&staging, header, 4), GX_OK);
	memset(header, 0, sizeof(header));
	KUNIT_EXPECT_EQ(test, gx_rx_header(&staging, fx_ack, 4), GX_BUSY);
	KUNIT_EXPECT_EQ(test, gx_rx_body(&staging, body, sizeof(body) - 1), GX_TRUNCATED);
	KUNIT_EXPECT_EQ(test, gx_rx_body(&staging, body, sizeof(body) + 1), GX_LENGTH);
	KUNIT_ASSERT_EQ(test, gx_rx_body(&staging, body, sizeof(body)), GX_OK);
	memset(body, 0, sizeof(body));
	KUNIT_EXPECT_TRUE(test, staging.ready);
	KUNIT_ASSERT_EQ(test, gx_parse_packet(staging.bytes, staging.size, &packet), GX_OK);
	KUNIT_ASSERT_EQ(test, gx_copy_version(&packet, version, sizeof(version)), GX_OK);
	gx_rx_reset(&staging);
	KUNIT_EXPECT_STREQ(test, version, FX_VERSION);
	KUNIT_EXPECT_EQ(test, staging.size, (size_t)0);
	KUNIT_EXPECT_EQ(test, staging.bytes[4], (u8)0);
}

static void staging_odd_and_bounds(struct kunit *test)
{
	struct gx_rx_staging staging;
	struct gx_packet packet;
	/* Synthetic string AB NUL: logical body 7, wire body 8. */
	u8 p[] = { 0xa0, 7, 0, 0, 0xa8, 4, 0, 'A', 'B', 0, 0, 0xff };
	u8 large[GX_MAX_PACKET] = { 0xa0, 0, 1, 0xa1, 0xa8, 0xfd, 0 };

	repair(p, sizeof(p) - 1);
	gx_rx_reset(&staging);
	KUNIT_ASSERT_EQ(test, gx_rx_header(&staging, p, 4), GX_OK);
	KUNIT_EXPECT_EQ(test, gx_rx_body(&staging, p + 4, 7), GX_TRUNCATED);
	KUNIT_ASSERT_EQ(test, gx_rx_body(&staging, p + 4, 8), GX_OK);
	KUNIT_EXPECT_EQ(test, staging.size, (size_t)11);
	KUNIT_EXPECT_EQ(test, gx_parse_packet(staging.bytes, staging.size, &packet), GX_OK);
	repair(large, sizeof(large));
	KUNIT_EXPECT_EQ(test, gx_rx_load(&staging, large, sizeof(large)), GX_OK);
	KUNIT_EXPECT_EQ(test, gx_parse_packet(staging.bytes, staging.size, &packet), GX_OK);
	KUNIT_EXPECT_EQ(test, packet.payload_size, (size_t)252);
}

static void ordered_events(struct kunit *test)
{
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	state(test, GX_WAIT_DATA, GX_OK);
	deliver(test, gen, fx_data, sizeof(fx_data), 2);
	state(test, GX_COMPLETE, GX_OK);
	/* Terminal result cannot be replaced by a late duplicate. */
	deliver(test, gen, fx_ack, sizeof(fx_ack), 3);
	state(test, GX_COMPLETE, GX_OK);
}

static void out_of_order(struct kunit *test)
{
	u64 gen = begin(test);

	deliver(test, gen, fx_data, sizeof(fx_data), 1);
	state(test, GX_FAILED, GX_UNEXPECTED);
	deliver(test, gen, fx_ack, sizeof(fx_ack), 2);
	state(test, GX_FAILED, GX_UNEXPECTED);
}

static void duplicate_ack(struct kunit *test)
{
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	deliver(test, gen, fx_ack, sizeof(fx_ack), 2);
	state(test, GX_FAILED, GX_UNEXPECTED);
}

static void rejected_ack(struct kunit *test)
{
	u8 p[sizeof(fx_ack)];
	u64 gen = begin(test);

	memcpy(p, fx_ack, sizeof(p));
	p[8] = 0;
	repair(p, sizeof(p));
	deliver(test, gen, p, sizeof(p), 1);
	state(test, GX_FAILED, GX_ACK_REJECTED);
}

static void invalid_async_packet(struct kunit *test)
{
	u8 p[sizeof(fx_ack)];
	u64 gen = begin(test);

	memcpy(p, fx_ack, sizeof(p));
	p[9]++;
	deliver(test, gen, p, sizeof(p), 1);
	state(test, GX_FAILED, GX_CHECKSUM);
	gen = begin(test);
	deliver(test, gen, fx_ack, sizeof(fx_ack) - 1, 1);
	state(test, GX_FAILED, GX_TRUNCATED);
}

static void invalid_version(struct kunit *test)
{
	u8 p[sizeof(fx_data)];
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	memcpy(p, fx_data, sizeof(p));
	p[sizeof(p) - 2] = 'X';
	repair(p, sizeof(p));
	deliver(test, gen, p, sizeof(p), 2);
	state(test, GX_FAILED, GX_STRING);
}

static void timeout_ack(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);

	KUNIT_EXPECT_EQ(test, gx_exchange_expire(&f->x, gen, 99), GX_BUSY);
	KUNIT_ASSERT_EQ(test, gx_test_source_timeout(&f->s, gen, 100), GX_OK);
	gx_test_source_flush(&f->s);
	state(test, GX_TIMED_OUT, GX_TIMEOUT);
	deliver(test, gen, fx_ack, sizeof(fx_ack), 101);
	state(test, GX_TIMED_OUT, GX_TIMEOUT);
}

static void timeout_data(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	KUNIT_EXPECT_EQ(test, gx_exchange_expire(&f->x, gen, 100), GX_BUSY);
	KUNIT_ASSERT_EQ(test, gx_test_source_timeout(&f->s, gen, 101), GX_OK);
	gx_test_source_flush(&f->s);
	state(test, GX_TIMED_OUT, GX_TIMEOUT);
}

static void late_packet_times_out(struct kunit *test)
{
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 100);
	state(test, GX_TIMED_OUT, GX_TIMEOUT);
}

static void cancel_processing(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);

	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, gen), GX_CANCELLED_ERROR);
	complete_all(&f->s.release);
	gx_test_source_flush(&f->s);
	state(test, GX_CANCELLED, GX_CANCELLED_ERROR);
}

static void cancel_data_processing(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 2, fx_data, sizeof(fx_data)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, gen), GX_CANCELLED_ERROR);
	complete_all(&f->s.release);
	gx_test_source_flush(&f->s);
	state(test, GX_CANCELLED, GX_CANCELLED_ERROR);
}

static void previous_generations(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 old = begin(test), gen;

	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, old), GX_CANCELLED_ERROR);
	gen = begin(test);
	KUNIT_EXPECT_GT(test, gen, old);
	deliver(test, old, fx_data, sizeof(fx_data), 1);
	KUNIT_ASSERT_EQ(test, gx_test_source_timeout(&f->s, old, 500), GX_OK);
	gx_test_source_flush(&f->s);
	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, old), GX_STALE);
	state(test, GX_WAIT_ACK, GX_OK);
	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	deliver(test, gen, fx_data, sizeof(fx_data), 2);
	state(test, GX_COMPLETE, GX_OK);
}

static void two_workers_and_owned_queue(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u8 data[sizeof(fx_data)];
	u64 gen = begin(test);

	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.concurrent, HZ * 5), 0UL);
	memcpy(data, fx_data, sizeof(data));
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 2, data, sizeof(data)), GX_OK);
	memset(data, 0, sizeof(data));
	complete_all(&f->s.release);
	gx_test_source_flush(&f->s);
	KUNIT_EXPECT_EQ(test, atomic_read(&f->s.peak_workers), 2);
	state(test, GX_COMPLETE, GX_OK);
}

static void queue_overflow(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);
	int i;

	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	for (i = 0; i < GX_QUEUE_DEPTH; i++)
		KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 2, fx_data, sizeof(fx_data)), GX_OK);
	KUNIT_EXPECT_EQ(test, gx_test_source_packet(&f->s, gen, 2, fx_data, sizeof(fx_data)), GX_OVERFLOW);
	complete_all(&f->s.release);
	gx_test_source_flush(&f->s);
	state(test, GX_FAILED, GX_OVERFLOW);
	KUNIT_EXPECT_EQ(test, f->s.count, 0U);
}

static void remove_processing(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test), ignored;

	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	gx_test_source_remove(&f->s);
	state(test, GX_REMOVED, GX_REMOVED_ERROR);
	KUNIT_EXPECT_EQ(test, gx_test_source_packet(&f->s, gen, 2, fx_data, sizeof(fx_data)), GX_REMOVED_ERROR);
	KUNIT_EXPECT_EQ(test, gx_exchange_begin(&f->x, 0, 100, 100, &ignored), GX_REMOVED_ERROR);
	KUNIT_EXPECT_EQ(test, atomic_read(&f->s.active_workers), 0);
}

static void cancelled_header_body(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	struct gx_rx_staging s;
	struct gx_prepared r;
	u64 gen = begin(test);

	gx_rx_reset(&s);
	KUNIT_ASSERT_EQ(test, gx_rx_header(&s, fx_ack, 4), GX_OK);
	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, gen), GX_CANCELLED_ERROR);
	KUNIT_ASSERT_EQ(test, gx_rx_body(&s, fx_ack + 4, sizeof(fx_ack) - 4), GX_OK);
	gx_exchange_prepare(gen, s.bytes, s.size, &r);
	KUNIT_EXPECT_EQ(test, gx_exchange_commit(&f->x, &r, 1), GX_STATE);
	state(test, GX_CANCELLED, GX_CANCELLED_ERROR);
}

struct race_job {
	struct work_struct work;
	struct completion *start;
	struct gx_exchange *x;
	struct gx_prepared *prepared;
	u64 gen;
	bool timeout;
};

static void race_work(struct work_struct *work)
{
	struct race_job *j = container_of(work, struct race_job, work);

	wait_for_completion(j->start);
	if (j->timeout)
		gx_exchange_expire(j->x, j->gen, 101);
	else
		gx_exchange_commit(j->x, j->prepared, 2);
}

static void completion_timeout_race(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	struct gx_prepared r;
	struct gx_result result;
	struct race_job jobs[2];
	DECLARE_COMPLETION_ONSTACK(start);
	u64 gen = begin(test);
	int i;

	deliver(test, gen, fx_ack, sizeof(fx_ack), 1);
	gx_exchange_prepare(gen, fx_data, sizeof(fx_data), &r);
	for (i = 0; i < 2; i++) {
		jobs[i].start = &start;
		jobs[i].x = &f->x;
		jobs[i].prepared = &r;
		jobs[i].gen = gen;
		jobs[i].timeout = i;
		INIT_WORK_ONSTACK(&jobs[i].work, race_work);
		queue_work(f->s.wq, &jobs[i].work);
	}
	complete_all(&start);
	gx_test_source_flush(&f->s);
	for (i = 0; i < 2; i++)
		destroy_work_on_stack(&jobs[i].work);
	gx_exchange_snapshot(&f->x, &result);
	KUNIT_EXPECT_TRUE(test, result.state == GX_COMPLETE || result.state == GX_TIMED_OUT);
	if (result.state == GX_COMPLETE)
		KUNIT_EXPECT_STREQ(test, result.version, FX_VERSION);
	else {
		KUNIT_EXPECT_EQ(test, result.error, GX_TIMEOUT);
		KUNIT_EXPECT_EQ(test, result.version[0], '\0');
	}
}

static void restart_with_old_worker(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 old = begin(test), gen;

	gx_test_source_pause(&f->s);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, old, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_NE(test, wait_for_completion_timeout(&f->s.prepared, HZ * 5), 0UL);
	KUNIT_EXPECT_EQ(test, gx_exchange_cancel(&f->x, old), GX_CANCELLED_ERROR);
	gen = begin(test);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, sizeof(fx_ack)), GX_OK);
	KUNIT_ASSERT_EQ(test, gx_test_source_packet(&f->s, gen, 2, fx_data, sizeof(fx_data)), GX_OK);
	complete_all(&f->s.release);
	gx_test_source_flush(&f->s);
	state(test, GX_COMPLETE, GX_OK);
}

static void source_bounds(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen = begin(test);

	KUNIT_EXPECT_EQ(test, gx_test_source_packet(&f->s, gen, 1, fx_ack, GX_MAX_PACKET + 1), GX_LIMIT);
	KUNIT_EXPECT_EQ(test, gx_test_source_packet(&f->s, gen, 1, NULL, 1), GX_ARGUMENT);
	KUNIT_EXPECT_EQ(test, atomic_read(&f->s.processed), 0);
	state(test, GX_WAIT_ACK, GX_OK);
}

static void ack_deadline_overflow(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	struct gx_prepared r;
	u64 gen;

	KUNIT_ASSERT_EQ(test, gx_exchange_begin(&f->x, U64_MAX - 10, 5, 100, &gen), GX_OK);
	gx_exchange_prepare(gen, fx_ack, sizeof(fx_ack), &r);
	KUNIT_EXPECT_EQ(test, gx_exchange_commit(&f->x, &r, U64_MAX - 9), GX_LIMIT);
	state(test, GX_FAILED, GX_LIMIT);
}

static void generation_and_deadline_bounds(struct kunit *test)
{
	struct gx_fixture *f = test->priv;
	u64 gen;

	KUNIT_EXPECT_EQ(test, gx_exchange_begin(&f->x, U64_MAX, 1, 1, &gen), GX_ARGUMENT);
	KUNIT_EXPECT_EQ(test, gx_exchange_begin(&f->x, 0, 0, 1, &gen), GX_ARGUMENT);
	f->x.result.generation = U64_MAX; /* No producer/worker scheduled. */
	KUNIT_EXPECT_EQ(test, gx_exchange_begin(&f->x, 0, 1, 1, &gen), GX_LIMIT);
}

static struct kunit_case cases[] = {
	KUNIT_CASE(real_protocol),
	KUNIT_CASE(malformed_protocol),
	KUNIT_CASE(staging_lifetime),
	KUNIT_CASE(staging_odd_and_bounds),
	KUNIT_CASE(ordered_events),
	KUNIT_CASE(out_of_order),
	KUNIT_CASE(duplicate_ack),
	KUNIT_CASE(rejected_ack),
	KUNIT_CASE(invalid_async_packet),
	KUNIT_CASE(invalid_version),
	KUNIT_CASE(timeout_ack),
	KUNIT_CASE(timeout_data),
	KUNIT_CASE(late_packet_times_out),
	KUNIT_CASE(cancel_processing),
	KUNIT_CASE(cancel_data_processing),
	KUNIT_CASE(previous_generations),
	KUNIT_CASE(two_workers_and_owned_queue),
	KUNIT_CASE(queue_overflow),
	KUNIT_CASE(remove_processing),
	KUNIT_CASE(cancelled_header_body),
	KUNIT_CASE(completion_timeout_race),
	KUNIT_CASE(generation_and_deadline_bounds),
	KUNIT_CASE(restart_with_old_worker),
	KUNIT_CASE(source_bounds),
	KUNIT_CASE(ack_deadline_overflow),
	{}
};

static struct kunit_suite suite = {
	.name = "gx-offline-v07",
	.init = setup,
	.exit = teardown,
	.test_cases = cases,
};
kunit_test_suite(suite);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Memory-only Goodix protocol KUnit tests; physical transport absent");
