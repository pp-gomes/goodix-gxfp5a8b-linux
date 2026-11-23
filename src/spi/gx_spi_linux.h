/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_SPI_LINUX_H
#define GX_SPI_LINUX_H

#include "gx_spi_transport.h"

struct spi_device;

/* Future context is the spi_device supplied by the reviewed ACPI bind.
 * Its owner must keep it alive until the session and callers are drained.
 * This executor is disabled at build time and currently returns -EOPNOTSUPP.
 */
int gx_spi_linux_execute(void *context, const struct gx_spi_message *message,
			 struct gx_spi_session *session,
			 struct gx_spi_receipt *receipt);

#endif
