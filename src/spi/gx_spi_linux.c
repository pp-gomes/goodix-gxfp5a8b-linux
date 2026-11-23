/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/acpi.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "gx_spi_linux.h"

/* Source-review gate. No Kconfig, module parameter or device registration. */
enum { GX_SPI_PHYSICAL_ENABLED = 0 };

static int check_client(struct spi_device *spi)
{
	struct acpi_device *adev;
	struct acpi_buffer path = { ACPI_ALLOCATE_BUFFER, NULL };
	struct device *parent;
	bool matches;

	if (!spi)
		return -ENODEV;
	adev = ACPI_COMPANION(&spi->dev);
	if (!adev || strcmp(acpi_device_hid(adev), "GXFP5A8B"))
		return -ENODEV;
	if (ACPI_FAILURE(acpi_get_name(adev->handle, ACPI_FULL_PATHNAME_NO_TRAILING, &path)))
		return -ENODEV;
	matches = !strcmp(path.pointer, "\\_SB.PCI0.SPI1.FPNT");
	kfree(path.pointer);
	if (!matches)
		return -ENODEV;

	/* The SPI core creates this client from FPNT's SPI connection resource.
	 * Check the enumerated controller lineage and the reviewed _CRS profile.
	 */
	for (parent = &spi->controller->dev; parent; parent = parent->parent) {
		if (dev_is_pci(parent))
			break;
	}
	if (!parent || to_pci_dev(parent)->vendor != PCI_VENDOR_ID_INTEL ||
	    to_pci_dev(parent)->device != 0x34ab)
		return -ENODEV;
	if (spi->num_chipselect != 1 || spi->cs_index_mask != 1 ||
	    spi_get_chipselect(spi, 0) != 0 || spi->mode != SPI_MODE_0 ||
	    spi->bits_per_word != 8 || spi->max_speed_hz != 10000000)
		return -EINVAL;
	return 0;
}

int gx_spi_linux_execute(void *context, const struct gx_spi_message *message,
			 struct gx_spi_session *session,
			 struct gx_spi_receipt *receipt)
{
	struct spi_device *spi = context;
	struct spi_transfer transfers[GX_SPI_MAX_TRANSFERS] = {};
	struct spi_message msg;
	unsigned int wire, rx_len, i, tx_offset = 0, rx_offset = 0;
	u8 *tx, *rx;
	int ret;

	if (!GX_SPI_PHYSICAL_ENABLED)
		return -EOPNOTSUPP;

	ret = check_client(spi);
	if (ret)
		return ret;
	ret = gx_spi_validate_message(message, &wire, &rx_len);
	if (ret)
		return ret;
	if (wire > spi_max_message_size(spi))
		return -EMSGSIZE;
	for (i = 0; i < message->count; i++) {
		if (message->transfers[i].len > spi_max_transfer_size(spi))
			return -EMSGSIZE;
	}

	/* Separate DMA-capable allocations for the two directions. */
	tx = kmalloc(GX_SPI_MAX_BYTES, GFP_KERNEL);
	rx = kzalloc(GX_SPI_CPU_RX_BYTES, GFP_KERNEL);
	if (!tx || !rx) {
		ret = -ENOMEM;
		goto free_buffers;
	}

	spi_message_init(&msg);
	for (i = 0; i < message->count; i++) {
		const struct gx_spi_transfer *transfer = &message->transfers[i];

		transfers[i].len = transfer->len;
		if (transfer->read) {
			transfers[i].rx_buf = rx + rx_offset;
			rx_offset += transfer->len;
		} else {
			memcpy(tx + tx_offset, transfer->tx, transfer->len);
			transfers[i].tx_buf = tx + tx_offset;
			tx_offset += transfer->len;
		}
		/* Zero cs_change keeps CS asserted between F1 and RX. */
		spi_message_add_tail(&transfers[i], &msg);
	}

	ret = gx_spi_stop_status(session);
	if (ret)
		goto free_buffers;
	ret = spi_sync(spi, &msg);
	receipt->actual_length = msg.actual_length;
	if (!ret && msg.actual_length == wire) {
		receipt->rx_length = rx_len;
		memcpy(receipt->rx, rx, rx_len);
	}

free_buffers:
	kfree(rx);
	kfree(tx);
	return ret;
}
