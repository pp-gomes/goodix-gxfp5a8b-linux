// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix GXFP5A8B passive ACPI/SPI diagnostic module (v0.2).
 *
 * This module deliberately does not perform SPI transfers, GPIO
 * requests/writes, IRQ registration or resets. It reads the SPI
 * metadata and evaluates the standard ACPI _CRS method to print
 * firmware-provided resource descriptors. _CRS is normally a query,
 * but running firmware AML is not a purely inert operation.
 */
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

static acpi_status gxfp_resource_cb(struct acpi_resource *resource, void *context)
{
    struct spi_device *spi = context;
    unsigned int i;

    if (resource->type == ACPI_RESOURCE_TYPE_GPIO) {
        const struct acpi_resource_gpio *gpio = &resource->data.gpio;
        const char *type = gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT
                         ? "GpioInt" : gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_IO
                         ? "GpioIo" : "unknown";
        dev_info(&spi->dev,
                 "ACPI %s: pins=%u trigger=%u polarity=%u io_restriction=%u wake_capable=%u controller=%s\n",
                 type, gpio->pin_table_length, gpio->triggering,
                 gpio->polarity, gpio->io_restriction,
                 gpio->wake_capable,
                 gpio->resource_source.string_ptr ? gpio->resource_source.string_ptr : "<none>");
        if (gpio->pin_table) {
            for (i = 0; i < gpio->pin_table_length && i < 16; i++)
                dev_info(&spi->dev, "ACPI %s pin[%u]=%u (firmware pin number, not necessarily gpiochip offset)\n",
                         type, i, gpio->pin_table[i]);
        }
    } else if (resource->type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
               resource->data.common_serial_bus.type == ACPI_RESOURCE_SERIAL_TYPE_SPI) {
        const struct acpi_resource_spi_serialbus *bus = &resource->data.spi_serial_bus;
        dev_info(&spi->dev,
                 "ACPI SpiSerialBus: cs=%u hz=%u bits=%u phase=%u polarity=%u cs_polarity=%u wires=%u\n",
                 bus->device_selection, bus->connection_speed,
                 bus->data_bit_length, bus->clock_phase,
                 bus->clock_polarity, bus->device_polarity,
                 bus->wire_mode);
    }
    return AE_OK;
}

static int gxfp5a8b_probe(struct spi_device *spi)
{
    acpi_handle handle = ACPI_HANDLE(&spi->dev);
    acpi_status status;

    dev_info(&spi->dev, "v0.2 passive probe: bus=%d cs=%u mode=0x%x bits=%u max_hz=%u irq=%d\n",
             spi->controller->bus_num, spi_get_chipselect(spi, 0),
             spi->mode, spi->bits_per_word, spi->max_speed_hz, spi->irq);

    if (!handle) {
        dev_warn(&spi->dev, "ACPI handle unavailable; _CRS not inspected\n");
        return 0;
    }

    status = acpi_walk_resources(handle, "_CRS", gxfp_resource_cb, spi);
    if (ACPI_FAILURE(status))
        dev_warn(&spi->dev, "ACPI _CRS enumeration failed: %s\n",
                 acpi_format_exception(status));
    return 0;
}

static void gxfp5a8b_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "v0.2 passive probe removed\n");
}

static const struct acpi_device_id gxfp5a8b_acpi_match[] = {
    { "GXFP5A8B", 0 }, { }
};
MODULE_DEVICE_TABLE(acpi, gxfp5a8b_acpi_match);

static const struct spi_device_id gxfp5a8b_spi_ids[] = {
    { "GXFP5A8B", 0 }, { }
};
MODULE_DEVICE_TABLE(spi, gxfp5a8b_spi_ids);

static struct spi_driver gxfp5a8b_driver = {
    .driver = {
        .name = "gxfp5a8b_probe",
        .acpi_match_table = gxfp5a8b_acpi_match,
    },
    .probe = gxfp5a8b_probe,
    .remove = gxfp5a8b_remove,
    .id_table = gxfp5a8b_spi_ids,
};
module_spi_driver(gxfp5a8b_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Read-only SPI and ACPI resource descriptor probe for GXFP5A8B");
MODULE_AUTHOR("Experimental engineering example");
