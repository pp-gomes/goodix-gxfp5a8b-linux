// SPDX-License-Identifier: GPL-2.0-only
/*
 * GXFP5A8B passive SPI probe.
 *
 * This is NOT a fingerprint driver.  It binds to the ACPI-enumerated
 * SPI peripheral and logs kernel-provided metadata.  It performs NO SPI
 * transfers, GPIO operations, IRQ requests, or firmware operations.
 *
 * Derived from public Linux SPI driver APIs; no Windows code included.
 */
#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

static int gxfp5a8b_probe(struct spi_device *spi)
{
    const unsigned int mode = spi->mode;
    const unsigned int cs = spi_get_chipselect(spi, 0);

    dev_info(&spi->dev,
             "passive probe: bus=%d cs=%u mode=0x%x bits_per_word=%u max_speed_hz=%u irq=%d\n",
             spi->controller->bus_num, cs, mode,
             spi->bits_per_word, spi->max_speed_hz, spi->irq);

    if (mode & (SPI_CPOL | SPI_CPHA | SPI_CS_HIGH | SPI_3WIRE))
        dev_warn(&spi->dev,
                 "unexpected SPI mode flags for documented LG Gram ACPI profile; no reconfiguration attempted\n");

    if (spi->max_speed_hz != 10000000)
        dev_warn(&spi->dev,
                 "clock differs from the 10 MHz DSDT value; no reconfiguration attempted\n");

    if (spi->bits_per_word != 8)
        dev_warn(&spi->dev,
                 "word length differs from 8-bit DSDT value; no reconfiguration attempted\n");

    if (spi->irq <= 0)
        dev_info(&spi->dev,
                 "SPI device irq field is not populated; ACPI GpioInt resource requires independent review\n");

    return 0;
}

static void gxfp5a8b_remove(struct spi_device *spi)
{
    dev_info(&spi->dev, "passive probe removed\n");
}

static const struct acpi_device_id gxfp5a8b_acpi_match[] = {
    { "GXFP5A8B", 0 },
    { }
};
MODULE_DEVICE_TABLE(acpi, gxfp5a8b_acpi_match);

static const struct spi_device_id gxfp5a8b_spi_ids[] = {
    { "GXFP5A8B", 0 },
    { }
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

MODULE_AUTHOR("Experimental reverse-engineering example");
MODULE_DESCRIPTION("Passive ACPI/SPI enumeration probe for Goodix GXFP5A8B");
MODULE_LICENSE("GPL");
