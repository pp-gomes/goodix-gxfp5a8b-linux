/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "gx_spi_linux.h"

static const struct acpi_device_id gx_spi_acpi_ids[] = {
	{ "GXFP5A8B", 0 },
	{ }
};

/* Preserve the descriptor in the review artifact without publishing aliases
 * or adding module entry points.
 */
static const struct spi_driver gx_spi_driver __used = {
	.driver = {
		.name = "goodix-gxfp5a8b",
		.acpi_match_table = gx_spi_acpi_ids,
	},
	.probe = gx_spi_linux_probe,
	.remove = gx_spi_linux_remove,
};

MODULE_DESCRIPTION("Goodix GXFP5A8B inert SPI integration artifact");
MODULE_LICENSE("GPL");
