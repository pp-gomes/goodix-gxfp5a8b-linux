/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_SPI_LINUX_H
#define GX_SPI_LINUX_H

#include "gx_spi_transport.h"

struct spi_device;

/* Internal adapter entry points. The executor remains compile-time disabled. */
int gx_spi_linux_check_client(struct spi_device *spi);
int gx_spi_linux_execute(void *context, const struct gx_spi_message *message,
			 struct gx_spi_session *session,
			 struct gx_spi_receipt *receipt);

/*
 * Unregistered driver callbacks for the ACPI-enumerated client. The owner
 * serializes probe/remove as the driver core would, and owns spi's drvdata.
 * All callers use sleepable context and hold a live spi_device reference.
 * No caller can retain or access the private session after returning.
 */
int gx_spi_linux_probe(struct spi_device *spi);
void gx_spi_linux_remove(struct spi_device *spi);
bool gx_spi_linux_cancel(struct spi_device *spi);
int gx_spi_linux_read_cpu(struct spi_device *spi, unsigned int timeout_ms,
			  struct gx_spi_result *result);

#endif
