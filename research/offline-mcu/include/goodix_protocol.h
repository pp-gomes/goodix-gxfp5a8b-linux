/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GOODIX_PROTOCOL_H
#define GOODIX_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Offline subset, not a complete MCU protocol or a hardware driver. */
#define GX_HEADER_SIZE 4u
#define GX_MAX_BODY 256u
#define GX_MAX_PACKET (GX_HEADER_SIZE + GX_MAX_BODY)
#define GX_MAX_VERSION 128u /* Includes the terminating NUL. */
#define GX_A8_SPI_SIZE 15u

enum gx_error {
    GX_OK = 0, GX_ARGUMENT = -1, GX_TRUNCATED = -2, GX_LENGTH = -3,
    GX_LIMIT = -4, GX_CHECKSUM = -5, GX_UNSUPPORTED = -6,
    GX_UNEXPECTED = -7, GX_BUFFER = -8, GX_ACK_REJECTED = -9,
    GX_STRING = -10, GX_STATE = -11, GX_TIMEOUT = -12,
    GX_HARDWARE_DISABLED = -13, GX_BUSY = -14
};

enum gx_kind { GX_DATA_A8, GX_ACK };
struct gx_header { size_t body_size; size_t wire_size; };
struct gx_packet {
    enum gx_kind kind;
    uint8_t opcode;
    const uint8_t *payload; /* Borrowed, valid only as long as input is valid. */
    size_t payload_size;
};

/* Only complete, exact-size inputs. Outputs unchanged on error, except written. */
int gx_parse_header(const uint8_t *data, size_t size, struct gx_header *out);
int gx_parse_packet(const uint8_t *data, size_t size, struct gx_packet *out);
/* Constructs the one observed A8 query IN MEMORY. No generic command writer. */
int gx_build_a8_spi(uint8_t *out, size_t capacity, size_t *written);
int gx_validate_a8_spi(const uint8_t *data, size_t size);
int gx_copy_version(const struct gx_packet *packet, char *out, size_t capacity);
/* Raw BE16 decode only: even FFFF is not interpreted as device availability. */
int gx_decode_cpu(const uint8_t *data, size_t size, uint16_t *out);

enum gx_exchange_state { GX_WAIT_ACK, GX_WAIT_DATA, GX_COMPLETE, GX_FAILED };
struct gx_exchange {
    enum gx_exchange_state state;
    int error;
    char version[GX_MAX_VERSION];
};
/* Explicit offline begin; never transmits. No retries, timers or recovery. */
void gx_exchange_init(struct gx_exchange *exchange);
int gx_exchange_receive(struct gx_exchange *exchange,
                        const uint8_t *data, size_t size);
int gx_exchange_expire(struct gx_exchange *exchange);

/* Synthetic IRQ -> header -> body -> worker-dispatch skeleton.
 * Caller must serialize all calls. These are NOT Linux IRQ callbacks.
 * No real IRQ registration, device handle, workqueue or read callback exists.
 */
enum gx_irq_stage { GX_IRQ_IDLE, GX_IRQ_HEADER, GX_IRQ_BODY };
struct gx_irq {
    enum gx_irq_stage stage;
    uint32_t events;
    struct gx_header expected;
    uint8_t frame[GX_MAX_PACKET];
    struct gx_exchange exchange;
};
void gx_irq_init(struct gx_irq *irq);
int gx_irq_notify(struct gx_irq *irq);
int gx_irq_feed_header(struct gx_irq *irq, const uint8_t *data, size_t size);
/* size must be the even-rounded wire_size; one alignment byte is ignored. */
int gx_irq_feed_body(struct gx_irq *irq, const uint8_t *data, size_t size);
int gx_irq_expire(struct gx_irq *irq);

/* Permanently disabled; no configuration switch or backend can enable it. */
int gx_hardware_submit_a8(void);

#endif
