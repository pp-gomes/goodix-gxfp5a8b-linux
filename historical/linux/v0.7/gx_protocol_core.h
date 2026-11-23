/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_PROTOCOL_CORE_H
#define GX_PROTOCOL_CORE_H

#include <linux/types.h>

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
    GX_HARDWARE_DISABLED = -13, GX_BUSY = -14, GX_CANCELLED_ERROR = -15, GX_STALE = -16,
    GX_OVERFLOW = -17, GX_REMOVED_ERROR = -18
};

enum gx_kind { GX_DATA_A8, GX_ACK };
struct gx_header { size_t body_size; size_t wire_size; };
struct gx_packet {
    enum gx_kind kind;
    u8 opcode;
    const u8 *payload; /* Borrowed, valid only as long as input is valid. */
    size_t payload_size;
};

/* Only complete, exact-size inputs. Outputs unchanged on error, except written. */
int gx_parse_header(const u8 *data, size_t size, struct gx_header *out);
int gx_parse_packet(const u8 *data, size_t size, struct gx_packet *out);
/* Constructs the one observed A8 query IN MEMORY. No generic command writer. */
int gx_build_a8_spi(u8 *out, size_t capacity, size_t *written);
int gx_validate_a8_spi(const u8 *data, size_t size);
int gx_copy_version(const struct gx_packet *packet, char *out, size_t capacity);
/* Raw BE16 decode only: even FFFF is not interpreted as device availability. */
int gx_decode_cpu(const u8 *data, size_t size, u16 *out);

#endif
