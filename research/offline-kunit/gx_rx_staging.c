/* SPDX-License-Identifier: GPL-2.0-only */
#include "gx_offline.h"
#include <linux/string.h>

void gx_rx_reset(struct gx_rx_staging *s)
{
	memset(s, 0, sizeof(*s));
}

int gx_rx_header(struct gx_rx_staging *s, const u8 *p, size_t n)
{
	struct gx_header h;
	int ret;

	if (!s)
		return GX_ARGUMENT;
	if (s->has_header)
		return GX_BUSY;
	ret = gx_parse_header(p, n, &h);
	if (ret)
		return ret;
	memcpy(s->bytes, p, GX_HEADER_SIZE);
	s->header = h;
	s->has_header = true;
	s->size = GX_HEADER_SIZE;
	return GX_OK;
}

int gx_rx_body(struct gx_rx_staging *s, const u8 *p, size_t n)
{
	if (!s || !p)
		return GX_ARGUMENT;
	if (!s->has_header || s->ready)
		return GX_STATE;
	if (n < s->header.wire_size)
		return GX_TRUNCATED;
	if (n != s->header.wire_size)
		return GX_LENGTH;
	memcpy(s->bytes + GX_HEADER_SIZE, p, s->header.body_size);
	s->size += s->header.body_size;
	s->ready = true;
	return GX_OK;
}

/* Exact logical frames, without an optional wire alignment byte. */
int gx_rx_load(struct gx_rx_staging *s, const u8 *p, size_t n)
{
	int ret;

	if (!s || !p)
		return GX_ARGUMENT;
	gx_rx_reset(s);
	if (n < GX_HEADER_SIZE)
		return GX_TRUNCATED;
	ret = gx_rx_header(s, p, GX_HEADER_SIZE);
	if (ret)
		return ret;
	if (n < GX_HEADER_SIZE + s->header.body_size)
		return GX_TRUNCATED;
	if (n != GX_HEADER_SIZE + s->header.body_size)
		return GX_LENGTH;
	memcpy(s->bytes + GX_HEADER_SIZE, p + GX_HEADER_SIZE, s->header.body_size);
	s->size = n;
	s->ready = true;
	return GX_OK;
}
