/* SPDX-License-Identifier: GPL-2.0-only */
#include "goodix_protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "windows_fixtures.h"

static unsigned checks;
#define CHECK(expr) do { checks++; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); exit(1); \
} } while (0)

static size_t join(uint8_t *out, const uint8_t *h, size_t hn,
                   const uint8_t *b, size_t bn)
{
    memcpy(out, h, hn);
    memcpy(out + hn, b, bn);
    return hn + bn;
}

static size_t ack(uint8_t *out)
{
    return join(out, fx_ack_header, sizeof(fx_ack_header),
                fx_ack_body, sizeof(fx_ack_body));
}

static size_t version(uint8_t *out)
{
    return join(out, fx_version_header, sizeof(fx_version_header),
                fx_version_body, sizeof(fx_version_body));
}

/* Synthetic malformed/boundary packet builder, test-only; no SPI envelope. */
static void repair_header(uint8_t *p)
{
    p[3] = (uint8_t)(p[0] + p[1] + p[2]);
}

static void repair_body(uint8_t *p, size_t n)
{
    size_t i;
    uint8_t sum = 0;
    for (i = 4; i + 1 < n; i++)
        sum = (uint8_t)(sum + p[i]);
    p[n - 1] = (uint8_t)(0xaa - sum);
}

static size_t synthetic(uint8_t *p, uint8_t opcode,
                         const uint8_t *payload, size_t size)
{
    size_t n = size + 8;
    CHECK(n <= GX_MAX_PACKET);
    memset(p, 0, n);
    p[0] = 0xa0;
    p[1] = (uint8_t)(size + 4);
    p[2] = (uint8_t)((size + 4) >> 8);
    repair_header(p);
    p[4] = opcode;
    p[5] = (uint8_t)(size + 1);
    p[6] = (uint8_t)((size + 1) >> 8);
    if (size)
        memcpy(p + 7, payload, size);
    repair_body(p, n);
    return n;
}

static void test_real_frames(void)
{
    uint8_t frame[GX_MAX_PACKET];
    struct gx_packet p;
    struct gx_header h;
    size_t n = ack(frame);
    char name[GX_MAX_VERSION];
    uint16_t cpu = 0;
    CHECK(gx_parse_header(frame, 4, &h) == GX_OK);
    CHECK(h.body_size == 6 && h.wire_size == 6);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(p.kind == GX_ACK && p.opcode == 0xb0 && p.payload_size == 2);
    CHECK(p.payload[0] == 0xa8 && p.payload[1] == 1);
    CHECK(gx_copy_version(&p, name, sizeof(name)) == GX_UNEXPECTED);
    n = version(frame);
    CHECK(gx_parse_header(frame, 4, &h) == GX_OK);
    CHECK(h.body_size == 30 && h.wire_size == 30);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(p.kind == GX_DATA_A8 && p.payload_size == sizeof(fx_firmware));
    CHECK(gx_copy_version(&p, name, sizeof(name)) == GX_OK);
    CHECK(strcmp(name, fx_firmware) == 0);
    CHECK(gx_decode_cpu(fx_cpu_rx, sizeof(fx_cpu_rx), &cpu) == GX_OK);
    CHECK(cpu == 0x00a1);
    CHECK(sizeof(fx_cpu_select_tx) == 3 && sizeof(fx_cpu_read_tx) == 1);
    CHECK(gx_decode_cpu((const uint8_t *)"\xff\xff", 2, &cpu) == GX_OK);
    CHECK(cpu == 0xffff); /* Raw decoding is not availability validation. */
}

static void test_builder(void)
{
    uint8_t output[GX_A8_SPI_SIZE + 2];
    size_t i, written;
    for (i = 0; i < GX_A8_SPI_SIZE; i++) {
        memset(output, 0x5c, sizeof(output));
        written = 123;
        CHECK(gx_build_a8_spi(output + 1, i, &written) == GX_BUFFER);
        CHECK(written == 0);
        CHECK(output[1] == 0x5c && output[sizeof(output) - 1] == 0x5c);
    }
    CHECK(gx_build_a8_spi(output + 1, GX_A8_SPI_SIZE, &written) == GX_OK);
    CHECK(written == sizeof(fx_request_spi));
    CHECK(memcmp(output + 1, fx_request_spi, written) == 0);
    CHECK(output[0] == 0x5c && output[sizeof(output) - 1] == 0x5c);
    CHECK(gx_validate_a8_spi(output + 1, written) == GX_OK);
    CHECK(gx_build_a8_spi(NULL, 20, &written) == GX_ARGUMENT && written == 0);
    CHECK(gx_build_a8_spi(output, sizeof(output), NULL) == GX_ARGUMENT);
}

static void test_truncation_and_extra(void)
{
    uint8_t p[GX_MAX_PACKET + 1];
    struct gx_packet packet;
    struct gx_header header;
    uint16_t cpu = 0xcccc;
    size_t i, j, n;
    for (j = 0; j < 2; j++) {
        n = j ? version(p) : ack(p);
        for (i = 0; i < n; i++)
            CHECK(gx_parse_packet(p, i, &packet) == GX_TRUNCATED);
        p[n] = 0;
        CHECK(gx_parse_packet(p, n + 1, &packet) == GX_LENGTH);
    }
    for (i = 0; i < 4; i++)
        CHECK(gx_parse_header(p, i, &header) == GX_TRUNCATED);
    CHECK(gx_parse_header(p, 5, &header) == GX_LENGTH);
    for (i = 0; i < sizeof(fx_request_spi); i++)
        CHECK(gx_validate_a8_spi(fx_request_spi, i) == GX_TRUNCATED);
    memcpy(p, fx_request_spi, sizeof(fx_request_spi));
    p[sizeof(fx_request_spi)] = 0;
    CHECK(gx_validate_a8_spi(p, sizeof(fx_request_spi) + 1) == GX_LENGTH);
    CHECK(gx_decode_cpu(fx_cpu_rx, 1, &cpu) == GX_TRUNCATED);
    CHECK(cpu == 0xcccc);
    CHECK(gx_decode_cpu(p, 3, &cpu) == GX_LENGTH);
}

static void test_mutations(void)
{
    uint8_t p[GX_MAX_PACKET], q[GX_MAX_PACKET];
    struct gx_packet packet;
    size_t i, bit, k, n;
    for (k = 0; k < 3; k++) {
        n = k == 0 ? ack(p) : version(p);
        if (k == 2) {
            n = sizeof(fx_request_spi);
            memcpy(p, fx_request_spi, n);
        }
        for (i = 0; i < n; i++) {
            for (bit = 0; bit < 8; bit++) {
                memcpy(q, p, n);
                q[i] ^= (uint8_t)(1u << bit);
                CHECK((k == 2 ? gx_validate_a8_spi(q, n) :
                        gx_parse_packet(q, n, &packet)) != GX_OK);
            }
        }
    }
}

static void test_lengths_and_flags(void)
{
    uint8_t p[GX_MAX_PACKET], payload[GX_MAX_BODY - 4];
    struct gx_header h = {77, 88};
    struct gx_packet packet;
    size_t i, n;
    static const size_t bad[] = {0, 1, 2, 3, 257, 65535};
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        ack(p);
        p[1] = (uint8_t)bad[i];
        p[2] = (uint8_t)(bad[i] >> 8);
        repair_header(p);
        CHECK(gx_parse_header(p, 4, &h) == (bad[i] < 4 ? GX_LENGTH : GX_LIMIT));
        CHECK(h.body_size == 77 && h.wire_size == 88);
    }
    for (i = 0; i < 256; i++) {
        if (i == 0xa0)
            continue;
        ack(p);
        p[0] = (uint8_t)i;
        repair_header(p);
        CHECK(gx_parse_header(p, 4, &h) == GX_UNSUPPORTED);
    }
    n = ack(p);
    p[5] = 0;
    repair_body(p, n);
    CHECK(gx_parse_packet(p, n, &packet) == GX_LENGTH);
    p[5] = 2;
    repair_body(p, n);
    CHECK(gx_parse_packet(p, n, &packet) == GX_LENGTH);
    p[5] = 3;
    p[6] = 1;
    repair_body(p, n);
    CHECK(gx_parse_packet(p, n, &packet) == GX_LENGTH);
    memset(payload, 'x', sizeof(payload));
    n = synthetic(p, 0xa8, payload, sizeof(payload));
    CHECK(n == GX_MAX_PACKET);
    CHECK(gx_parse_packet(p, n, &packet) == GX_OK);
    CHECK(packet.payload_size == sizeof(payload));
    n = synthetic(p, 0xb0, payload, 1);
    CHECK(gx_parse_packet(p, n, &packet) == GX_LENGTH);
    n = synthetic(p, 0xb0, payload, 3);
    CHECK(gx_parse_packet(p, n, &packet) == GX_LENGTH);
    n = synthetic(p, 0xa8, NULL, 0);
    CHECK(gx_parse_packet(p, n, &packet) == GX_OK);
}

static void test_version_buffers(void)
{
    uint8_t frame[GX_MAX_PACKET], payload[GX_MAX_BODY - 4];
    char out[GX_MAX_VERSION + 2];
    struct gx_packet p;
    size_t n, capacity;
    n = version(frame);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    for (capacity = 0; capacity < sizeof(fx_firmware); capacity++) {
        memset(out, '#', sizeof(out));
        CHECK(gx_copy_version(&p, out + 1, capacity) == GX_BUFFER);
        CHECK(out[0] == '#' && out[1] == '#' && out[sizeof(out) - 1] == '#');
    }
    CHECK(gx_copy_version(&p, out + 1, sizeof(fx_firmware)) == GX_OK);
    CHECK(out[0] == '#' && out[sizeof(fx_firmware) + 1] == '#');
    memset(payload, 'a', sizeof(payload));
    payload[GX_MAX_VERSION - 1] = 0;
    n = synthetic(frame, 0xa8, payload, GX_MAX_VERSION);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(gx_copy_version(&p, out + 1, GX_MAX_VERSION) == GX_OK);
    payload[GX_MAX_VERSION - 1] = 'a';
    payload[GX_MAX_VERSION] = 0;
    n = synthetic(frame, 0xa8, payload, GX_MAX_VERSION + 1);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(gx_copy_version(&p, out, sizeof(out)) == GX_LIMIT);
    n = synthetic(frame, 0xa8, NULL, 0);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(gx_copy_version(&p, out, sizeof(out)) == GX_STRING);
    n = synthetic(frame, 0xa8, (const uint8_t *)"", 1);
    CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
    CHECK(gx_copy_version(&p, out, sizeof(out)) == GX_STRING);
    /* Missing NUL, embedded NUL, non-ASCII and control character. */
    for (capacity = 0; capacity < 4; capacity++) {
        static const uint8_t invalid[4][3] = {
            {'a','b','c'}, {'a',0,0}, {'a',0x80,0}, {'a',0x1f,0}
        };
        n = synthetic(frame, 0xa8, invalid[capacity], 3);
        CHECK(gx_parse_packet(frame, n, &p) == GX_OK);
        CHECK(gx_copy_version(&p, out, sizeof(out)) == GX_STRING);
    }
}

static void test_exchange(void)
{
    struct gx_exchange ex;
    uint8_t frame[GX_MAX_PACKET];
    size_t n;
    gx_exchange_init(&ex);
    CHECK(ex.state == GX_WAIT_ACK && ex.version[0] == 0);
    n = ack(frame);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_OK);
    CHECK(ex.state == GX_WAIT_DATA && ex.version[0] == 0);
    n = version(frame);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_OK);
    CHECK(ex.state == GX_COMPLETE && strcmp(ex.version, fx_firmware) == 0);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_STATE);
    CHECK(gx_exchange_expire(&ex) == GX_STATE);
    CHECK(ex.state == GX_COMPLETE && strcmp(ex.version, fx_firmware) == 0);
    gx_exchange_init(&ex);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_UNEXPECTED); /* Data first. */
    CHECK(ex.state == GX_FAILED && ex.version[0] == 0);
    n = ack(frame);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_STATE); /* No auto retry. */
    gx_exchange_init(&ex);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_OK);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_UNEXPECTED); /* Duplicate ACK. */
    gx_exchange_init(&ex);
    frame[7] = 0xa6; /* Wrong acknowledged command, valid checksum. */
    repair_body(frame, n);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_UNEXPECTED);
    gx_exchange_init(&ex);
    ack(frame);
    frame[8] = 0;
    repair_body(frame, n);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_ACK_REJECTED);
    gx_exchange_init(&ex);
    n = ack(frame);
    frame[4] = 0xa9; /* No silent normalization of unknown opcodes. */
    repair_body(frame, n);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_UNEXPECTED);
    gx_exchange_init(&ex);
    CHECK(gx_exchange_expire(&ex) == GX_TIMEOUT);
    CHECK(ex.state == GX_FAILED && ex.error == GX_TIMEOUT);
    gx_exchange_init(&ex);
    n = ack(frame);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_OK);
    CHECK(gx_exchange_expire(&ex) == GX_TIMEOUT);
    gx_exchange_init(&ex);
    CHECK(gx_exchange_receive(&ex, frame, n - 1) == GX_TRUNCATED);
    CHECK(ex.state == GX_FAILED && ex.error == GX_TRUNCATED);
}

static void test_valid_checksums_unexpected_responses(void)
{
    uint8_t frame[GX_MAX_PACKET];
    struct gx_exchange ex;
    struct gx_packet packet;
    struct gx_irq irq;
    size_t n, i;
    uint8_t payload[2] = {0xa8, 2};
    /* Structurally valid rejection and wrong opcode, not checksum failures. */
    gx_exchange_init(&ex);
    n = synthetic(frame, 0xb0, payload, 2);
    CHECK(gx_parse_packet(frame, n, &packet) == GX_OK);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_ACK_REJECTED);
    n = synthetic(frame, 0xa2, payload, 2);
    CHECK(gx_parse_packet(frame, n, &packet) == GX_UNEXPECTED);
    n = synthetic(frame, 0xa8, payload, 2);
    CHECK(gx_parse_packet(frame, n, &packet) == GX_OK);
    CHECK(gx_copy_version(&packet, ex.version, sizeof(ex.version)) == GX_STRING);
    gx_exchange_init(&ex);
    n = ack(frame);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_OK);
    n = synthetic(frame, 0xa8, payload, 2);
    CHECK(gx_exchange_receive(&ex, frame, n) == GX_STRING);
    CHECK(ex.state == GX_FAILED && ex.version[0] == 0);
    /* Request with valid checksums, but unexpected nonzero request payload. */
    memcpy(frame, fx_request_spi, sizeof(fx_request_spi));
    frame[12] = 1;
    repair_body(frame + 5, sizeof(fx_request_spi) - 5);
    CHECK(gx_validate_a8_spi(frame, sizeof(fx_request_spi)) == GX_UNEXPECTED);
    memcpy(frame, fx_request_spi, sizeof(fx_request_spi));
    frame[3] = 10;
    frame[4] = 0; /* BE16 vs LE16 confusion. */
    CHECK(gx_validate_a8_spi(frame, sizeof(fx_request_spi)) == GX_LIMIT);
    /* Every invalid header/body ends the pending exchange, never copies excess. */
    for (i = 0; i < 3; i++) {
        gx_irq_init(&irq);
        n = ack(frame);
        CHECK(gx_irq_notify(&irq) == GX_OK);
        if (i == 0) {
            frame[1] = 1;
            frame[2] = 1;
            repair_header(frame);
            CHECK(gx_irq_feed_header(&irq, frame, 4) == GX_LIMIT);
        } else {
            CHECK(gx_irq_feed_header(&irq, frame, 4) == GX_OK);
            if (i == 1) {
                frame[n - 1] ^= 1;
                CHECK(gx_irq_feed_body(&irq, frame + 4, n - 4) == GX_CHECKSUM);
            } else {
                CHECK(gx_irq_feed_body(&irq, NULL, n - 4) == GX_ARGUMENT);
            }
        }
        CHECK(irq.exchange.state == GX_FAILED && irq.stage == GX_IRQ_IDLE);
        CHECK(irq.frame[0] == 0 && irq.exchange.version[0] == 0);
    }
}

static void feed_irq(struct gx_irq *irq, const uint8_t *h, size_t hn,
                      const uint8_t *b, size_t bn)
{
    CHECK(gx_irq_notify(irq) == GX_OK);
    CHECK(gx_irq_feed_header(irq, h, hn) == GX_OK);
    CHECK(gx_irq_feed_body(irq, b, bn) == GX_OK);
}

static void test_irq_replay(void)
{
    struct gx_irq irq;
    gx_irq_init(&irq);
    CHECK(irq.stage == GX_IRQ_IDLE && irq.events == 0);
    CHECK(gx_irq_feed_header(&irq, fx_ack_header, 4) == GX_STATE);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_notify(&irq) == GX_BUSY);
    CHECK(irq.stage == GX_IRQ_HEADER && irq.events == 2);
    CHECK(gx_irq_feed_body(&irq, fx_ack_body, sizeof(fx_ack_body)) == GX_STATE);
    CHECK(gx_irq_feed_header(&irq, fx_ack_header, 4) == GX_OK);
    CHECK(irq.expected.body_size == 6);
    CHECK(gx_irq_notify(&irq) == GX_BUSY);
    CHECK(gx_irq_feed_body(&irq, fx_ack_body, sizeof(fx_ack_body)) == GX_OK);
    CHECK(irq.stage == GX_IRQ_IDLE && irq.exchange.state == GX_WAIT_DATA);
    feed_irq(&irq, fx_version_header, sizeof(fx_version_header),
             fx_version_body, sizeof(fx_version_body));
    CHECK(irq.exchange.state == GX_COMPLETE);
    CHECK(strcmp(irq.exchange.version, fx_firmware) == 0);
    CHECK(irq.frame[0] == 0 && irq.expected.body_size == 0);
    CHECK(gx_irq_notify(&irq) == GX_STATE);
}

static void test_irq_errors_and_padding(void)
{
    struct gx_irq irq;
    uint8_t frame[GX_MAX_PACKET], wire[GX_MAX_BODY];
    size_t n;
    gx_irq_init(&irq);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_feed_header(&irq, fx_ack_header, 3) == GX_TRUNCATED);
    CHECK(irq.exchange.state == GX_FAILED && irq.stage == GX_IRQ_IDLE);
    CHECK(gx_irq_notify(&irq) == GX_STATE);
    gx_irq_init(&irq);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_feed_header(&irq, fx_ack_header, 4) == GX_OK);
    CHECK(gx_irq_feed_body(&irq, fx_ack_body, 5) == GX_TRUNCATED);
    gx_irq_init(&irq);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_feed_header(&irq, fx_ack_header, 4) == GX_OK);
    memset(wire, 0, sizeof(wire));
    CHECK(gx_irq_feed_body(&irq, wire, 7) == GX_LENGTH);
    gx_irq_init(&irq);
    feed_irq(&irq, fx_ack_header, 4, fx_ack_body, 6);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_expire(&irq) == GX_TIMEOUT);
    CHECK(irq.stage == GX_IRQ_IDLE && irq.exchange.state == GX_FAILED);
    gx_irq_init(&irq);
    feed_irq(&irq, fx_ack_header, 4, fx_ack_body, 6);
    /* Synthetic odd-sized body; transport padding has no checksum semantics. */
    n = synthetic(frame, 0xa8, (const uint8_t *)"ab", 3);
    CHECK(n == 11);
    memcpy(wire, frame + 4, 7);
    wire[7] = 0x5a;
    feed_irq(&irq, frame, 4, wire, 8);
    CHECK(strcmp(irq.exchange.version, "ab") == 0);
    gx_irq_init(&irq);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    irq.events = UINT32_MAX;
    CHECK(gx_irq_notify(&irq) == GX_BUSY && irq.events == UINT32_MAX);
}

static void test_null_and_unchanged_outputs(void)
{
    struct gx_packet p;
    struct gx_header h;
    struct gx_irq irq;
    uint16_t cpu;
    uint8_t frame[GX_MAX_PACKET];
    size_t n = ack(frame);
    memset(&p, 0x5a, sizeof(p));
    memset(&h, 0x5a, sizeof(h));
    CHECK(gx_parse_packet(NULL, 0, &p) == GX_ARGUMENT);
    CHECK(gx_parse_packet(frame, n, NULL) == GX_ARGUMENT);
    CHECK(gx_parse_header(NULL, 4, &h) == GX_ARGUMENT);
    CHECK(gx_parse_header(frame, 4, NULL) == GX_ARGUMENT);
    frame[3] ^= 1;
    CHECK(gx_parse_packet(frame, n, &p) == GX_CHECKSUM);
    CHECK(((uint8_t *)&p)[0] == 0x5a);
    CHECK(gx_copy_version(NULL, (char *)frame, 1) == GX_ARGUMENT);
    CHECK(gx_decode_cpu(NULL, 2, &cpu) == GX_ARGUMENT);
    CHECK(gx_decode_cpu(frame, 2, NULL) == GX_ARGUMENT);
    CHECK(gx_validate_a8_spi(NULL, 0) == GX_ARGUMENT);
    CHECK(gx_exchange_receive(NULL, frame, n) == GX_ARGUMENT);
    CHECK(gx_exchange_expire(NULL) == GX_ARGUMENT);
    CHECK(gx_irq_notify(NULL) == GX_ARGUMENT);
    CHECK(gx_irq_feed_header(NULL, frame, 4) == GX_ARGUMENT);
    CHECK(gx_irq_feed_body(NULL, frame, 6) == GX_ARGUMENT);
    CHECK(gx_irq_expire(NULL) == GX_ARGUMENT);
    gx_irq_init(NULL);
    gx_exchange_init(NULL);
    gx_irq_init(&irq);
    CHECK(gx_irq_notify(&irq) == GX_OK);
    CHECK(gx_irq_feed_header(&irq, NULL, 4) == GX_ARGUMENT);
    CHECK(irq.exchange.state == GX_FAILED);
}

static void test_deterministic_random_inputs(void)
{
    uint8_t input[GX_MAX_PACKET + 16];
    struct gx_packet packet;
    struct gx_irq irq;
    uint32_t seed = 0x6a8;
    size_t i, j, n;
    int ret;
    for (i = 0; i < 20000; i++) {
        seed = seed * 1664525u + 1013904223u;
        n = seed % sizeof(input);
        for (j = 0; j < sizeof(input); j++) {
            seed = seed * 1664525u + 1013904223u;
            input[j] = (uint8_t)(seed >> 24);
        }
        ret = gx_parse_packet(input, n, &packet);
        CHECK(ret <= GX_OK);
        if (ret == GX_OK) {
            CHECK(packet.payload >= input + 7);
            CHECK(packet.payload + packet.payload_size <= input + n - 1);
        }
        gx_irq_init(&irq);
        CHECK(gx_irq_notify(&irq) == GX_OK);
        ret = gx_irq_feed_header(&irq, input, n < 4 ? n : 4);
        if (ret == GX_OK)
            (void)gx_irq_feed_body(&irq, input, n);
        CHECK(gx_hardware_submit_a8() == GX_HARDWARE_DISABLED);
    }
}

int main(void)
{
    static const struct { const char *name; void (*run)(void); } tests[] = {
        {"real Windows frames", test_real_frames},
        {"A8 builder and output capacity", test_builder},
        {"all truncated prefixes and extra bytes", test_truncation_and_extra},
        {"all single-bit corruptions", test_mutations},
        {"lengths, bounds and unsupported flags", test_lengths_and_flags},
        {"version strings and buffer guards", test_version_buffers},
        {"ACK/data state machine and timeouts", test_exchange},
        {"valid checksums with unexpected responses", test_valid_checksums_unexpected_responses},
        {"synthetic IRQ replay and backpressure", test_irq_replay},
        {"IRQ errors, expiry and padding", test_irq_errors_and_padding},
        {"null arguments and output integrity", test_null_and_unchanged_outputs},
        {"20000 deterministic random inputs", test_deterministic_random_inputs}
    };
    size_t i;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        tests[i].run();
        printf("PASS %s\n", tests[i].name);
    }
    printf("PASS %u checks; hardware backend permanently disabled\n", checks);
    return 0;
}
