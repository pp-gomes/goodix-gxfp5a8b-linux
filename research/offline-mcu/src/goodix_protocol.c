/* SPDX-License-Identifier: GPL-2.0-only */
#include "goodix_protocol.h"
#include <string.h>

static size_t le16(const uint8_t *p)
{
    return (size_t)p[0] | ((size_t)p[1] << 8);
}

static uint8_t sum8(const uint8_t *p, size_t n)
{
    uint8_t sum = 0;
    size_t i;
    for (i = 0; i < n; i++)
        sum = (uint8_t)(sum + p[i]);
    return sum;
}

int gx_parse_header(const uint8_t *data, size_t size, struct gx_header *out)
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

int gx_parse_packet(const uint8_t *data, size_t size, struct gx_packet *out)
{
    struct gx_header h;
    struct gx_packet packet;
    const uint8_t *body;
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

int gx_build_a8_spi(uint8_t *out, size_t capacity, size_t *written)
{
    uint8_t frame[GX_A8_SPI_SIZE] = {0};
    uint8_t *mcu = frame + 5;
    uint8_t *body = mcu + GX_HEADER_SIZE;
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
    body[5] = (uint8_t)(0xaa - sum8(body, 5));
    memcpy(out, frame, sizeof(frame));
    *written = sizeof(frame);
    return GX_OK;
}

int gx_validate_a8_spi(const uint8_t *data, size_t size)
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

int gx_decode_cpu(const uint8_t *data, size_t size, uint16_t *out)
{
    if (!data || !out)
        return GX_ARGUMENT;
    if (size < 2)
        return GX_TRUNCATED;
    if (size != 2)
        return GX_LENGTH;
    *out = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return GX_OK;
}

void gx_exchange_init(struct gx_exchange *exchange)
{
    if (exchange) {
        memset(exchange, 0, sizeof(*exchange));
        exchange->state = GX_WAIT_ACK;
    }
}

static int fail(struct gx_exchange *exchange, int error)
{
    exchange->state = GX_FAILED;
    exchange->error = error;
    memset(exchange->version, 0, sizeof(exchange->version));
    return error;
}

int gx_exchange_receive(struct gx_exchange *exchange,
                        const uint8_t *data, size_t size)
{
    struct gx_packet packet;
    int ret;
    if (!exchange)
        return GX_ARGUMENT;
    if (exchange->state != GX_WAIT_ACK && exchange->state != GX_WAIT_DATA)
        return GX_STATE; /* Terminal state and result retained, no retry. */
    ret = gx_parse_packet(data, size, &packet);
    if (ret != GX_OK)
        return fail(exchange, ret);
    if (exchange->state == GX_WAIT_ACK) {
        if (packet.kind != GX_ACK || packet.payload[0] != 0xa8)
            return fail(exchange, GX_UNEXPECTED);
        if (packet.payload[1] != 1)
            return fail(exchange, GX_ACK_REJECTED);
        exchange->state = GX_WAIT_DATA;
        return GX_OK;
    }
    if (packet.kind != GX_DATA_A8)
        return fail(exchange, GX_UNEXPECTED);
    ret = gx_copy_version(&packet, exchange->version, sizeof(exchange->version));
    if (ret != GX_OK)
        return fail(exchange, ret);
    exchange->state = GX_COMPLETE;
    return GX_OK;
}

int gx_exchange_expire(struct gx_exchange *exchange)
{
    if (!exchange)
        return GX_ARGUMENT;
    if (exchange->state != GX_WAIT_ACK && exchange->state != GX_WAIT_DATA)
        return GX_STATE;
    return fail(exchange, GX_TIMEOUT);
}

void gx_irq_init(struct gx_irq *irq)
{
    if (irq) {
        memset(irq, 0, sizeof(*irq));
        gx_exchange_init(&irq->exchange);
    }
}

int gx_irq_notify(struct gx_irq *irq)
{
    if (!irq)
        return GX_ARGUMENT;
    if (irq->exchange.state != GX_WAIT_ACK && irq->exchange.state != GX_WAIT_DATA)
        return GX_STATE;
    if (irq->events != UINT32_MAX)
        irq->events++;
    if (irq->stage != GX_IRQ_IDLE)
        return GX_BUSY; /* Never overwrite a pending frame. */
    irq->stage = GX_IRQ_HEADER;
    return GX_OK;
}

static int irq_fail(struct gx_irq *irq, int error)
{
    irq->stage = GX_IRQ_IDLE;
    memset(&irq->expected, 0, sizeof(irq->expected));
    memset(irq->frame, 0, sizeof(irq->frame));
    return fail(&irq->exchange, error);
}

int gx_irq_feed_header(struct gx_irq *irq, const uint8_t *data, size_t size)
{
    struct gx_header header;
    int ret;
    if (!irq)
        return GX_ARGUMENT;
    if (irq->stage != GX_IRQ_HEADER)
        return GX_STATE;
    ret = gx_parse_header(data, size, &header);
    if (ret != GX_OK)
        return irq_fail(irq, ret);
    irq->expected = header;
    memcpy(irq->frame, data, GX_HEADER_SIZE);
    irq->stage = GX_IRQ_BODY;
    return GX_OK;
}

int gx_irq_feed_body(struct gx_irq *irq, const uint8_t *data, size_t size)
{
    int ret;
    if (!irq)
        return GX_ARGUMENT;
    if (irq->stage != GX_IRQ_BODY)
        return GX_STATE;
    if (!data)
        return irq_fail(irq, GX_ARGUMENT);
    if (size < irq->expected.wire_size)
        return irq_fail(irq, GX_TRUNCATED);
    if (size != irq->expected.wire_size)
        return irq_fail(irq, GX_LENGTH);
    /* Header was bounded before any copy; ignore only the alignment byte. */
    memcpy(irq->frame + GX_HEADER_SIZE, data, irq->expected.body_size);
    ret = gx_exchange_receive(&irq->exchange, irq->frame,
                               GX_HEADER_SIZE + irq->expected.body_size);
    irq->stage = GX_IRQ_IDLE;
    memset(&irq->expected, 0, sizeof(irq->expected));
    memset(irq->frame, 0, sizeof(irq->frame));
    return ret;
}

int gx_irq_expire(struct gx_irq *irq)
{
    if (!irq)
        return GX_ARGUMENT;
    if (irq->exchange.state != GX_WAIT_ACK && irq->exchange.state != GX_WAIT_DATA)
        return GX_STATE;
    return irq_fail(irq, GX_TIMEOUT);
}

int gx_hardware_submit_a8(void)
{
    return GX_HARDWARE_DISABLED;
}
