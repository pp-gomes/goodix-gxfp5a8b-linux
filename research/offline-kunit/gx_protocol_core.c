/* SPDX-License-Identifier: GPL-2.0-only */
#include "gx_protocol_core.h"
#include <linux/string.h>

static size_t le16(const u8 *p)
{
    return (size_t)p[0] | ((size_t)p[1] << 8);
}

static u8 sum8(const u8 *p, size_t n)
{
    u8 sum = 0;
    size_t i;
    for (i = 0; i < n; i++)
        sum = (u8)(sum + p[i]);
    return sum;
}

int gx_parse_header(const u8 *data, size_t size, struct gx_header *out)
{
    struct gx_header h;
    if (!data || !out)
        return GX_ARGUMENT;
    if (size < GX_HEADER_SIZE)
        return GX_TRUNCATED;
    if (size != GX_HEADER_SIZE)
        return GX_LENGTH;
    /* Do not silently mask unverified flags/mailbox routes or fragmentation. */
    if (data[0] != 0xa0)
        return GX_UNSUPPORTED;
    if (sum8(data, 3) != data[3])
        return GX_CHECKSUM;
    h.body_size = le16(data + 1);
    if (h.body_size < 4)
        return GX_LENGTH;
    if (h.body_size > GX_MAX_BODY)
        return GX_LIMIT;
    h.wire_size = (h.body_size + 1u) & ~(size_t)1u;
    *out = h;
    return GX_OK;
}

int gx_parse_packet(const u8 *data, size_t size, struct gx_packet *out)
{
    struct gx_header h;
    struct gx_packet packet;
    const u8 *body;
    int ret;
    if (!data || !out)
        return GX_ARGUMENT;
    if (size < GX_HEADER_SIZE)
        return GX_TRUNCATED;
    ret = gx_parse_header(data, GX_HEADER_SIZE, &h);
    if (ret != GX_OK)
        return ret;
    if (size < GX_HEADER_SIZE + h.body_size)
        return GX_TRUNCATED;
    if (size != GX_HEADER_SIZE + h.body_size)
        return GX_LENGTH;
    body = data + GX_HEADER_SIZE;
    /* Inner length includes payload + checksum, excludes opcode and lenLE. */
    if (le16(body + 1) < 1 || le16(body + 1) != h.body_size - 3u)
        return GX_LENGTH;
    if (sum8(body, h.body_size) != 0xaa)
        return GX_CHECKSUM;
    packet.opcode = body[0];
    packet.payload = body + 3;
    packet.payload_size = h.body_size - 4u;
    if (packet.opcode == 0xb0) {
        if (packet.payload_size != 2)
            return GX_LENGTH;
        packet.kind = GX_ACK;
    } else if (packet.opcode == 0xa8) {
        packet.kind = GX_DATA_A8;
    } else {
        return GX_UNEXPECTED;
    }
    *out = packet;
    return GX_OK;
}

int gx_build_a8_spi(u8 *out, size_t capacity, size_t *written)
{
    u8 frame[GX_A8_SPI_SIZE] = {0};
    u8 *mcu = frame + 5;
    u8 *body = mcu + GX_HEADER_SIZE;
    if (!written)
        return GX_ARGUMENT;
    *written = 0;
    if (!out)
        return GX_ARGUMENT;
    if (capacity < sizeof(frame))
        return GX_BUFFER;
    frame[0] = 0xf0;
    frame[1] = 0x3d;
    frame[2] = 0x00;
    /* SPI envelope length is BE16; MCU lengths are LE16. */
    frame[3] = 0;
    frame[4] = 10;
    mcu[0] = 0xa0;
    mcu[1] = 6;
    mcu[2] = 0;
    mcu[3] = sum8(mcu, 3);
    body[0] = 0xa8;
    body[1] = 3;
    body[2] = 0;
    body[3] = 0;
    body[4] = 0;
    body[5] = (u8)(0xaa - sum8(body, 5));
    memcpy(out, frame, sizeof(frame));
    *written = sizeof(frame);
    return GX_OK;
}

int gx_validate_a8_spi(const u8 *data, size_t size)
{
    struct gx_packet packet;
    size_t declared;
    int ret;
    if (!data)
        return GX_ARGUMENT;
    if (size < 5)
        return GX_TRUNCATED;
    if (data[0] != 0xf0 || data[1] != 0x3d || data[2] != 0)
        return GX_UNEXPECTED;
    declared = ((size_t)data[3] << 8) | data[4];
    if (declared > GX_MAX_PACKET)
        return GX_LIMIT;
    if (declared != 10)
        return GX_LENGTH;
    if (size < 5 + declared)
        return GX_TRUNCATED;
    if (size != 5 + declared)
        return GX_LENGTH;
    ret = gx_parse_packet(data + 5, declared, &packet);
    if (ret != GX_OK)
        return ret;
    if (packet.kind != GX_DATA_A8 || packet.payload_size != 2 ||
        packet.payload[0] != 0 || packet.payload[1] != 0)
        return GX_UNEXPECTED;
    return GX_OK;
}

int gx_copy_version(const struct gx_packet *packet, char *out, size_t capacity)
{
    size_t i;
    if (!packet || !out || !packet->payload)
        return GX_ARGUMENT;
    if (packet->kind != GX_DATA_A8 || packet->opcode != 0xa8)
        return GX_UNEXPECTED;
    if (packet->payload_size < 2)
        return GX_STRING;
    if (packet->payload_size > GX_MAX_VERSION)
        return GX_LIMIT;
    if (packet->payload[packet->payload_size - 1] != 0)
        return GX_STRING;
    /* The supported profile is one nonempty, printable ASCII, NUL-ended string. */
    for (i = 0; i + 1 < packet->payload_size; i++)
        if (packet->payload[i] < 0x20 || packet->payload[i] > 0x7e)
            return GX_STRING;
    if (capacity < packet->payload_size)
        return GX_BUFFER;
    memcpy(out, packet->payload, packet->payload_size);
    return GX_OK;
}

int gx_decode_cpu(const u8 *data, size_t size, u16 *out)
{
    if (!data || !out)
        return GX_ARGUMENT;
    if (size < 2)
        return GX_TRUNCATED;
    if (size != 2)
        return GX_LENGTH;
    *out = (u16)(((u16)data[0] << 8) | data[1]);
    return GX_OK;
}

