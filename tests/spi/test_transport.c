/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include "gx_spi_transport.h"

#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define BUDGET_MS 5000

struct fake_spi {
	struct gx_spi_receipt replies[2];
	atomic_uint calls;
	u64 deadlines[4];
	unsigned int pause_at;
	bool wait_for_stop;
	atomic_bool entered;
	atomic_bool release;
	atomic_int stop;
};

struct task {
	struct gx_spi_session *session;
	struct gx_spi_result result;
	atomic_bool started;
	atomic_bool done;
	int ret;
};

static void tick(void)
{
	const struct timespec pause = { .tv_nsec = 1000000 };

	nanosleep(&pause, NULL);
}

static void wait_for(atomic_bool *flag)
{
	unsigned int i;

	for (i = 0; i < BUDGET_MS && !atomic_load(flag); i++)
		tick();
	assert(atomic_load(flag));
}

static int fake_execute(void *context, const struct gx_spi_message *message,
			struct gx_spi_session *session, struct gx_spi_receipt *receipt)
{
	struct fake_spi *fake = context;
	unsigned int index = atomic_fetch_add(&fake->calls, 1);

	assert(index < 4);
	fake->deadlines[index] = session->deadline_ns;
	if (index % 2 == 0) {
		const u8 selector[] = { 0xf0, 0x00, 0xa8 };

		assert(message->count == 1 && !message->transfers[0].read);
		assert(message->transfers[0].len == sizeof(selector));
		assert(!memcmp(message->transfers[0].tx, selector, sizeof(selector)));
	} else {
		assert(message->count == 2);
		assert(!message->transfers[0].read && message->transfers[0].len == 1);
		assert(message->transfers[0].tx[0] == 0xf1);
		assert(message->transfers[1].read && message->transfers[1].len == 2);
	}
	if (fake->pause_at == index + 1) {
		atomic_store(&fake->entered, true);
		if (fake->wait_for_stop) {
			int stop;

			do {
				stop = gx_spi_stop_status(session);
				if (!stop)
					tick();
			} while (!stop);
			atomic_store(&fake->stop, stop);
		}
		wait_for(&fake->release);
	}
	*receipt = fake->replies[index % 2];
	return receipt->status;
}

static struct fake_spi reference(void)
{
	return (struct fake_spi) {
		.replies = {
			{ .actual_length = 3 },
			{ .actual_length = 3, .rx_length = 2, .rx = { 0x00, 0xa1 } },
		},
	};
}

static void destroy(struct gx_spi_session *session)
{
	gx_spi_remove(session);
	assert(!pthread_mutex_destroy(&session->state));
	assert(!pthread_mutex_destroy(&session->operation));
}

static void test_limits(void)
{
	struct gx_spi_message message = { 0 };
	struct gx_spi_session session;
	struct gx_spi_result result;
	struct fake_spi fake = reference();
	unsigned int wire, rx;

	assert(gx_spi_validate_message(NULL, &wire, &rx) == -EINVAL);
	assert(gx_spi_validate_message(&message, &wire, &rx) == -EINVAL);
	message.count = GX_SPI_MAX_TRANSFERS + 1;
	assert(gx_spi_validate_message(&message, &wire, &rx) == -EINVAL);
	message.count = 1;
	assert(gx_spi_validate_message(&message, &wire, &rx) == -EINVAL);
	message.transfers[0].len = 4;
	assert(gx_spi_validate_message(&message, &wire, &rx) == -EMSGSIZE);
	message.transfers[0].len = 3;
	message.transfers[0].read = true;
	assert(gx_spi_validate_message(&message, &wire, &rx) == -EMSGSIZE);
	message.transfers[0].read = false;
	assert(!gx_spi_validate_message(&message, &wire, &rx));
	assert(wire == 3 && rx == 0);

	assert(gx_spi_session_init(NULL, fake_execute, &fake) == -EINVAL);
	assert(gx_spi_session_init(&session, NULL, &fake) == -EINVAL);
	assert(!gx_spi_session_init(&session, fake_execute, &fake));
	assert(gx_spi_read_cpu(NULL, 1, &result) == -EINVAL);
	assert(gx_spi_read_cpu(&session, 1, NULL) == -EINVAL);
	assert(gx_spi_read_cpu(&session, 0, &result) == -EINVAL);
	assert(gx_spi_read_cpu(&session, GX_SPI_MAX_TIMEOUT_MS + 1, &result) == -EINVAL);
	assert(!result.count && !atomic_load(&fake.calls));
	destroy(&session);
}

static void test_reply(struct fake_spi *fake, int expected, unsigned int count)
{
	struct gx_spi_session session;
	struct gx_spi_result result;
	unsigned int i;

	assert(!gx_spi_session_init(&session, fake_execute, fake));
	assert(gx_spi_read_cpu(&session, BUDGET_MS, &result) == expected);
	assert(result.count == count && atomic_load(&fake->calls) == count);
	if (count == 2)
		assert(fake->deadlines[0] == fake->deadlines[1]);
	for (i = 0; i < count; i++) {
		const struct gx_spi_receipt *r = &result.receipts[i];
		const struct gx_spi_receipt *expected_receipt = &fake->replies[i];

		assert(r->status == expected_receipt->status);
		assert(r->actual_length == expected_receipt->actual_length);
		if (expected_receipt->rx_length > (i ? GX_SPI_CPU_RX_BYTES : 0)) {
			const u8 empty[GX_SPI_CPU_RX_BYTES] = { 0 };

			assert(!r->rx_length);
			assert(!memcmp(r->rx, empty, sizeof(r->rx)));
		} else {
			assert(r->rx_length == expected_receipt->rx_length);
			assert(!memcmp(r->rx, expected_receipt->rx, sizeof(r->rx)));
		}
	}
	destroy(&session);
}

static void test_responses(void)
{
	struct fake_spi fake = reference();

	test_reply(&fake, 0, 2);
	fake = reference();
	memset(fake.replies[1].rx, 0xff, 2);
	test_reply(&fake, -EBADMSG, 2);
	fake = reference();
	fake.replies[1].rx[0] = 0xa1;
	fake.replies[1].rx[1] = 0;
	test_reply(&fake, -EBADMSG, 2);
}

static void test_errors(void)
{
	struct fake_spi fake = reference();

	fake.replies[0].status = -EIO;
	fake.replies[0].actual_length = 1;
	test_reply(&fake, -EIO, 1);
	fake = reference();
	fake.replies[1].status = -ESHUTDOWN;
	fake.replies[1].actual_length = 2;
	fake.replies[1].rx_length = 1;
	fake.replies[1].rx[1] = 0;
	test_reply(&fake, -ESHUTDOWN, 2);
	fake = reference();
	fake.replies[1].status = -ETIMEDOUT;
	test_reply(&fake, -ETIMEDOUT, 2);
}

static void test_lengths(void)
{
	struct fake_spi fake = reference();

	fake.replies[0].actual_length = 2;
	test_reply(&fake, -EMSGSIZE, 1);
	fake = reference();
	fake.replies[1].actual_length = 4;
	test_reply(&fake, -EMSGSIZE, 2);
	fake = reference();
	fake.replies[1].rx_length = 1;
	test_reply(&fake, -EMSGSIZE, 2);
	fake = reference();
	fake.replies[1].rx_length = 3;
	test_reply(&fake, -EMSGSIZE, 2);
	fake = reference();
	fake.replies[1].rx_length = 3;
	fake.replies[1].status = -EIO;
	test_reply(&fake, -EIO, 2);
	fake = reference();
	fake.replies[0].rx_length = 1;
	fake.replies[0].rx[0] = 0xff;
	test_reply(&fake, -EMSGSIZE, 1);
}

static void test_deadline(void)
{
	struct fake_spi fake = reference();
	struct gx_spi_session session;
	struct gx_spi_result result;

	fake.pause_at = 1;
	fake.wait_for_stop = true;
	atomic_store(&fake.release, true);
	assert(!gx_spi_session_init(&session, fake_execute, &fake));
	assert(gx_spi_read_cpu(&session, 200, &result) == -ETIMEDOUT);
	assert(atomic_load(&fake.stop) == -ETIMEDOUT);
	assert(result.count == 1 && result.receipts[0].status == 0);
	assert(atomic_load(&fake.calls) == 1);
	destroy(&session);
}

static void *reader(void *context)
{
	struct task *task = context;

	atomic_store(&task->started, true);
	task->ret = gx_spi_read_cpu(task->session, BUDGET_MS, &task->result);
	atomic_store(&task->done, true);
	return NULL;
}

static void *remover(void *context)
{
	struct task *task = context;

	gx_spi_remove(task->session);
	atomic_store(&task->done, true);
	return NULL;
}

static void test_cancel(void)
{
	struct fake_spi fake = reference();
	struct gx_spi_session session;
	struct task read = { .session = &session };
	struct gx_spi_result result;
	pthread_t thread;

	/* Cancel inside the second callback and retain both receipts. */
	fake.pause_at = 2;
	fake.wait_for_stop = true;
	assert(!gx_spi_session_init(&session, fake_execute, &fake));
	assert(!gx_spi_cancel(&session));
	assert(!pthread_create(&thread, NULL, reader, &read));
	wait_for(&fake.entered);
	assert(gx_spi_cancel(&session));
	atomic_store(&fake.release, true);
	assert(!pthread_join(thread, NULL));
	assert(atomic_load(&fake.stop) == -ECANCELED);
	assert(read.ret == -ECANCELED && read.result.count == 2);
	assert(!gx_spi_cancel(&session));
	assert(!gx_spi_read_cpu(&session, BUDGET_MS, &result) && result.count == 2);
	destroy(&session);
}

static void test_remove(void)
{
	struct fake_spi fake = reference();
	struct gx_spi_session session;
	struct task read = { .session = &session }, removal = { .session = &session };
	struct task queued = { .session = &session };
	pthread_t thread, removing, waiting;
	unsigned int i;

	fake.pause_at = 1;
	fake.wait_for_stop = true;
	assert(!gx_spi_session_init(&session, fake_execute, &fake));
	assert(!pthread_create(&thread, NULL, reader, &read));
	wait_for(&fake.entered);
	assert(!pthread_create(&waiting, NULL, reader, &queued));
	wait_for(&queued.started);
	assert(!pthread_create(&removing, NULL, remover, &removal));
	for (i = 0; i < BUDGET_MS && !atomic_load(&fake.stop); i++)
		tick();
	assert(atomic_load(&fake.stop) == -ENODEV);
	assert(!atomic_load(&removal.done) && !gx_spi_cancel(&session));
	atomic_store(&fake.release, true);
	assert(!pthread_join(thread, NULL));
	assert(!pthread_join(removing, NULL));
	assert(!pthread_join(waiting, NULL));
	assert(read.ret == -ENODEV && read.result.count == 1);
	assert(queued.ret == -ENODEV && !queued.result.count);
	assert(atomic_load(&fake.calls) == 1);
	destroy(&session);
}

static void test_serialization(void)
{
	struct fake_spi fake = reference();
	struct gx_spi_session session;
	struct task first = { .session = &session }, second = { .session = &session };
	pthread_t thread1, thread2;

	fake.pause_at = 1;
	assert(!gx_spi_session_init(&session, fake_execute, &fake));
	assert(!pthread_create(&thread1, NULL, reader, &first));
	wait_for(&fake.entered);
	assert(!pthread_create(&thread2, NULL, reader, &second));
	wait_for(&second.started);
	assert(pthread_mutex_trylock(&session.operation) == EBUSY);
	assert(atomic_load(&fake.calls) == 1);
	atomic_store(&fake.release, true);
	assert(!pthread_join(thread1, NULL));
	assert(!pthread_join(thread2, NULL));
	assert(!first.ret && first.result.count == 2);
	assert(!second.ret && second.result.count == 2);
	assert(atomic_load(&fake.calls) == 4);
	destroy(&session);
}

int main(void)
{
	test_limits();
	test_responses();
	test_errors();
	test_lengths();
	test_deadline();
	test_cancel();
	test_remove();
	test_serialization();
	puts("8 SPI transport integration tests passed");
	return 0;
}
