/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/acpi.h>
#include <linux/mfd/core.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pxa2xx_ssp.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "gx_spi_linux.h"

/* Source-review gate. No Kconfig, module parameter or device registration. */
enum { GX_SPI_PHYSICAL_ENABLED = 0 };

#ifdef CONFIG_ACPI
static bool is_unique_client(struct acpi_device *adev, struct device *dev)
{
	struct acpi_device_physical_node *node;
	bool matches = false;

	mutex_lock(&adev->physical_node_lock);
	node = list_first_entry_or_null(&adev->physical_node_list,
					struct acpi_device_physical_node, node);
	if (adev->physical_node_count == 1 && node)
		matches = node->dev == dev;
	mutex_unlock(&adev->physical_node_lock);
	return matches;
}

/* One SPI controller may share a companion with its PCI, MFD and DMA nodes. */
static bool is_unique_controller(struct acpi_device *adev, struct device *ctlr,
				 struct device *host, struct device *pci)
{
	struct acpi_device_physical_node *node;
	bool found_ctlr = false, found_host = false, found_pci = false;

	mutex_lock(&adev->physical_node_lock);
	list_for_each_entry(node, &adev->physical_node_list, node) {
		struct device *dev = node->dev;

		if (dev_is_pci(dev)) {
			if (dev != pci)
				goto ambiguous;
			found_pci = true;
		} else if (dev->class == ctlr->class) {
			if (dev != ctlr)
				goto ambiguous;
			found_ctlr = true;
		} else if (dev == host) {
			found_host = true;
		}
	}
	mutex_unlock(&adev->physical_node_lock);
	return found_ctlr && found_host && found_pci;

ambiguous:
	mutex_unlock(&adev->physical_node_lock);
	return false;
}
#endif

int gx_spi_linux_check_client(struct spi_device *spi)
{
#ifdef CONFIG_ACPI
	struct acpi_device *adev, *acpi_parent;
	const struct mfd_cell *cell;
	struct device *host, *pci;
	acpi_handle client_handle, source;
	u32 type;

	if (!spi || !spi->controller || spi->dev.parent != &spi->controller->dev)
		return -ENODEV;

	adev = ACPI_COMPANION(&spi->dev);
	if (!adev || strcmp(acpi_device_hid(adev), "GXFP5A8B") ||
	    !acpi_device_enumerated(adev) || !is_unique_client(adev, &spi->dev))
		return -ENODEV;
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.PCI0.SPI1.FPNT",
					 &client_handle)) ||
	    adev->handle != client_handle)
		return -ENODEV;

	/*
	 * Resolve the previously reviewed ResourceSource in the namespace.
	 * Reuse the SPI core's parent match without evaluating _CRS again.
	 */
	acpi_parent = acpi_dev_parent(adev);
	if (ACPI_FAILURE(acpi_get_handle(NULL, "\\_SB_.PCI0.SPI1", &source)) ||
	    !acpi_parent || acpi_parent->handle != source)
		return -ENODEV;

	host = spi->controller->dev.parent;
	if (!host || !dev_is_platform(host) ||
	    !device_match_acpi_handle(host, source))
		return -ENODEV;
	pci = host->parent;
	if (!pci || !dev_is_pci(pci) || !device_match_acpi_handle(pci, source) ||
	    to_pci_dev(pci)->vendor != PCI_VENDOR_ID_INTEL ||
	    to_pci_dev(pci)->device != 0x34ab)
		return -ENODEV;

	cell = mfd_get_cell(to_platform_device(host));
	if (!cell || strcmp(cell->name, "pxa2xx-spi") || !cell->swnode ||
	    fwnode_property_read_u32(software_node_fwnode(cell->swnode),
				    "intel,spi-pxa2xx-type", &type) ||
	    type != LPSS_CNL_SSP || !spi->controller->dev.class ||
	    !is_unique_controller(acpi_parent, &spi->controller->dev, host, pci))
		return -ENODEV;

	if (spi->num_chipselect != 1 || spi->cs_index_mask != 1 ||
	    spi_get_chipselect(spi, 0) != 0 || spi_get_csgpiod(spi, 0) ||
	    spi->mode != SPI_MODE_0 || spi->bits_per_word != 8 ||
	    spi->max_speed_hz != 10000000)
		return -EINVAL;
	return 0;
#else
	return -ENODEV;
#endif
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

	ret = gx_spi_linux_check_client(spi);
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
