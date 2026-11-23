/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __KERNEL__
#define _POSIX_C_SOURCE 200809L
#endif
#include "gx_spi_transport.h"

#ifdef __KERNEL__
#include <linux/errno.h>
#include <linux/ktime.h>
#include <linux/string.h>

static u64 now_ns(void)
{
	return ktime_get_ns();
}
#else
#include <errno.h>
#include <string.h>
#include <time.h>

#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock

static u64 now_ns(void)
{
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);
	return (u64)now.tv_sec * 1000000000ULL + (u64)now.tv_nsec;
}
#endif

static const struct gx_spi_message cpu_messages[] = {
	{ .count = 1, .transfers = { { .tx = { 0xf0, 0x00, 0xa8 }, .len = 3 } } },
	{ .count = 2, .transfers = {
		{ .tx = { 0xf1 }, .len = 1 },
		{ .len = GX_SPI_CPU_RX_BYTES, .read = true },
	} },
};

int gx_spi_validate_message(const struct gx_spi_message *message,
			    unsigned int *wire_length, unsigned int *rx_length)
{
	unsigned int i, wire = 0, rx = 0;

	if (!message || !wire_length || !rx_length || !message->count ||
	    message->count > GX_SPI_MAX_TRANSFERS)
		return -EINVAL;

	for (i = 0; i < message->count; i++) {
		const struct gx_spi_transfer *transfer = &message->transfers[i];

		if (!transfer->len)
			return -EINVAL;
		wire += transfer->len;
		if (transfer->read)
			rx += transfer->len;
	}
	if (wire > GX_SPI_MAX_BYTES || rx > GX_SPI_CPU_RX_BYTES)
		return -EMSGSIZE;
	*wire_length = wire;
	*rx_length = rx;
	return 0;
}

int gx_spi_session_init(struct gx_spi_session *session, gx_spi_execute execute,
			void *context)
{
#ifndef __KERNEL__
	int ret;
#endif

	if (!session || !execute)
		return -EINVAL;

	memset(session, 0, sizeof(*session));
#ifdef __KERNEL__
	mutex_init(&session->operation);
	mutex_init(&session->state);
#else
	ret = pthread_mutex_init(&session->operation, NULL);
	if (ret)
		return -ret;
	ret = pthread_mutex_init(&session->state, NULL);
	if (ret) {
		pthread_mutex_destroy(&session->operation);
		return -ret;
	}
#endif
	session->execute = execute;
	session->context = context;
	return 0;
}

static int stop_status_locked(const struct gx_spi_session *session)
{
	if (session->removed)
		return -ENODEV;
	if (session->cancelled)
		return -ECANCELED;
	if (session->active && now_ns() >= session->deadline_ns)
		return -ETIMEDOUT;
	return 0;
}

int gx_spi_stop_status(struct gx_spi_session *session)
{
	int ret;

	mutex_lock(&session->state);
	ret = stop_status_locked(session);
	mutex_unlock(&session->state);
	return ret;
}

bool gx_spi_cancel(struct gx_spi_session *session)
{
	bool active;

	mutex_lock(&session->state);
	active = session->active && !session->removed;
	if (active)
		session->cancelled = true;
	mutex_unlock(&session->state);
	return active;
}

void gx_spi_remove(struct gx_spi_session *session)
{
	mutex_lock(&session->state);
	session->removed = true;
	mutex_unlock(&session->state);

	mutex_lock(&session->operation);
	mutex_unlock(&session->operation);
}

static int execute_message(struct gx_spi_session *session,
			   const struct gx_spi_message *message,
			   struct gx_spi_result *result)
{
	struct gx_spi_receipt *receipt;
	unsigned int wire, rx;
	int ret;

	ret = gx_spi_validate_message(message, &wire, &rx);
	if (ret)
		return ret;
	ret = gx_spi_stop_status(session);
	if (ret)
		return ret;

	receipt = &result->receipts[result->count++];
	receipt->status = session->execute(session->context, message, session,
					   receipt);
	if (receipt->rx_length > rx) {
		receipt->rx_length = 0;
		memset(receipt->rx, 0, sizeof(receipt->rx));
		return receipt->status ? receipt->status : -EMSGSIZE;
	}
	if (receipt->status)
		return receipt->status;
	if (receipt->actual_length != wire || receipt->rx_length != rx)
		return -EMSGSIZE;
	return 0;
}

int gx_spi_read_cpu(struct gx_spi_session *session, unsigned int timeout_ms,
		    struct gx_spi_result *result)
{
	const u8 *rx;
	int ret, stopped;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));
	if (!session || !timeout_ms || timeout_ms > GX_SPI_MAX_TIMEOUT_MS)
		return -EINVAL;

	mutex_lock(&session->operation);
	mutex_lock(&session->state);
	if (session->removed) {
		mutex_unlock(&session->state);
		mutex_unlock(&session->operation);
		return -ENODEV;
	}
	session->active = true;
	session->cancelled = false;
	session->deadline_ns = now_ns() + (u64)timeout_ms * 1000000ULL;
	mutex_unlock(&session->state);

	ret = execute_message(session, &cpu_messages[0], result);
	if (!ret)
		ret = execute_message(session, &cpu_messages[1], result);
	if (!ret) {
		rx = result->receipts[1].rx;
		if (rx[0] != 0x00 || rx[1] != 0xa1)
			ret = -EBADMSG;
	}

	mutex_lock(&session->state);
	stopped = stop_status_locked(session);
	session->active = false;
	session->cancelled = false;
	mutex_unlock(&session->state);
	mutex_unlock(&session->operation);
	return stopped ? stopped : ret;
}
