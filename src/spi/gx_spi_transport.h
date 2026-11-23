/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_SPI_TRANSPORT_H
#define GX_SPI_TRANSPORT_H

#ifdef __KERNEL__
#include <linux/mutex.h>
#include <linux/types.h>
#else
/* Build the same transport core for the host integration tests. */
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint64_t u64;
typedef pthread_mutex_t gx_spi_mutex;
#endif

#ifdef __KERNEL__
typedef struct mutex gx_spi_mutex;
#endif

#define GX_SPI_MAX_TRANSFERS 2
#define GX_SPI_MAX_BYTES 3
#define GX_SPI_CPU_RX_BYTES 2
#define GX_SPI_MAX_TIMEOUT_MS 60000U

struct gx_spi_transfer {
	u8 tx[GX_SPI_MAX_BYTES];
	u8 len;
	bool read;
};

/* One CS window. No CS change between transfers. */
struct gx_spi_message {
	u8 count;
	struct gx_spi_transfer transfers[GX_SPI_MAX_TRANSFERS];
};

struct gx_spi_receipt {
	int status;
	unsigned int actual_length;
	/* Published RX is limited to the amount requested by the message. */
	unsigned int rx_length;
	u8 rx[GX_SPI_CPU_RX_BYTES];
};

struct gx_spi_result {
	unsigned int count;
	struct gx_spi_receipt receipts[2];
};

struct gx_spi_session;
typedef int (*gx_spi_execute)(void *context, const struct gx_spi_message *message,
			      struct gx_spi_session *session,
			      struct gx_spi_receipt *receipt);

struct gx_spi_session {
	gx_spi_mutex operation;
	gx_spi_mutex state;
	gx_spi_execute execute;
	void *context;
	u64 deadline_ns;
	bool active;
	bool cancelled;
	bool removed;
};

/*
 * One session per client. All calls are from sleepable context. The owner
 * keeps session/context live until remove and all callers have returned.
 * The synchronous executor must release all buffer references before return,
 * report zero or negative errno and publish only confirmed RX. It can poll
 * gx_spi_stop_status(), but must never call read or remove on this session.
 */
int gx_spi_session_init(struct gx_spi_session *session, gx_spi_execute execute,
			void *context);
int gx_spi_validate_message(const struct gx_spi_message *message,
			    unsigned int *wire_length, unsigned int *rx_length);
int gx_spi_stop_status(struct gx_spi_session *session);
bool gx_spi_cancel(struct gx_spi_session *session);
void gx_spi_remove(struct gx_spi_session *session);
/*
 * Deadline starts on admission and is shared across both messages. A running
 * synchronous execute cannot be forcibly interrupted. Receipts survive stop.
 * Returns -EBADMSG for invalid response and -EMSGSIZE for length mismatch.
 */
int gx_spi_read_cpu(struct gx_spi_session *session, unsigned int timeout_ms,
		    struct gx_spi_result *result);

#endif
