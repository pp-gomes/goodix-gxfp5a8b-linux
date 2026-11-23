// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix GXFP5A8B: passive ACPI/SPI probe, optional IRQ event counter (v0.3)
 *
 * observe_irq=0 (default): identical observation-only intent to v0.2.
 * observe_irq=1: request existing spi->irq and count events, no SPI/GPIO I/O.
 *
 * WARNING: requesting an IRQ enables its handler and may change IRQ controller
 * masking state. This is not a completely passive experiment. Kernel module
 * testing can destabilize an OS even when this driver is deliberately minimal.
 */
#include <linux/acpi.h>
#include <linux/atomic.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

static bool observe_irq;
module_param(observe_irq, bool, 0444);
MODULE_PARM_DESC(observe_irq,
                 "Enable existing SPI IRQ for event counting (default: false)");

struct gxfp_probe_ctx {
    atomic64_t irq_events;
};

static irqreturn_t gxfp_irq_handler(int irq, void *data)
{
    struct gxfp_probe_ctx *ctx = data;

    atomic64_inc(&ctx->irq_events);
    return IRQ_HANDLED;
}

static ssize_t irq_events_show(struct device *dev,
                               struct device_attribute *attr, char *buf)
{
    struct gxfp_probe_ctx *ctx = dev_get_drvdata(dev);

    if (!ctx)
        return -ENODEV;
    return sysfs_emit(buf, "%lld\n",
                      (long long)atomic64_read(&ctx->irq_events));
}
static DEVICE_ATTR_RO(irq_events);

static acpi_status gxfp_resource_cb(struct acpi_resource *resource, void *data)
{
    struct spi_device *spi = data;
    unsigned int i;

    if (resource->type == ACPI_RESOURCE_TYPE_GPIO) {
        const struct acpi_resource_gpio *gpio = &resource->data.gpio;
        const char *kind = gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_INT ?
                           "GpioInt" : gpio->connection_type == ACPI_RESOURCE_GPIO_TYPE_IO ?
                           "GpioIo" : "UnknownGPIO";

        dev_info(&spi->dev,
                 "ACPI %s: pins=%u triggering=%u polarity=%u io_restriction=%u wake_capable=%u controller=%s\n",
                 kind, gpio->pin_table_length, gpio->triggering,
                 gpio->polarity, gpio->io_restriction,
                 gpio->wake_capable,
                 gpio->resource_source.string_ptr ?
                 gpio->resource_source.string_ptr : "<none>");
        for (i = 0; gpio->pin_table && i < gpio->pin_table_length && i < 8; i++)
            dev_info(&spi->dev, "ACPI %s pin[%u]=%u (firmware numbering)\n",
                     kind, i, gpio->pin_table[i]);
    } else if (resource->type == ACPI_RESOURCE_TYPE_SERIAL_BUS &&
               resource->data.common_serial_bus.type == ACPI_RESOURCE_SERIAL_TYPE_SPI) {
        const struct acpi_resource_spi_serialbus *bus = &resource->data.spi_serial_bus;

        dev_info(&spi->dev,
                 "ACPI SpiSerialBus: cs=%u hz=%u bits=%u phase=%u polarity=%u cs_polarity=%u\n",
                 bus->device_selection, bus->connection_speed,
                 bus->data_bit_length, bus->clock_phase,
                 bus->clock_polarity, bus->device_polarity);
    }
    return AE_OK;
}

static int gxfp5a8b_probe(struct spi_device *spi)
{
    struct gxfp_probe_ctx *ctx;
    acpi_handle handle = ACPI_HANDLE(&spi->dev);
    acpi_status status;
    int ret;

    ctx = devm_kzalloc(&spi->dev, sizeof(*ctx), GFP_KERNEL);
    if (!ctx)
        return -ENOMEM;
    atomic64_set(&ctx->irq_events, 0);
    spi_set_drvdata(spi, ctx);

    dev_info(&spi->dev,
             "v0.3 probe: bus=%d cs=%u mode=0x%x bits=%u max_hz=%u irq=%d observe_irq=%d\n",
             spi->controller->bus_num, spi_get_chipselect(spi, 0),
             spi->mode, spi->bits_per_word, spi->max_speed_hz,
             spi->irq, observe_irq);

    if (handle) {
        status = acpi_walk_resources(handle, "_CRS", gxfp_resource_cb, spi);
        if (ACPI_FAILURE(status))
            dev_warn(&spi->dev, "_CRS inspection failed: %s\n",
                     acpi_format_exception(status));
    }

    ret = device_create_file(&spi->dev, &dev_attr_irq_events);
    if (ret)
        return dev_err_probe(&spi->dev, ret, "could not create read-only event counter\n");

    if (observe_irq) {
        if (spi->irq <= 0) {
            ret = -ENXIO;
            dev_err(&spi->dev, "no usable IRQ, refuse observer mode\n");
            goto remove_attribute;
        }
        /* Use the triggering already selected by ACPI/kernel. */
        ret = devm_request_irq(&spi->dev, spi->irq, gxfp_irq_handler,
                               0, "gxfp5a8b-observer", ctx);
        if (ret) {
            dev_err(&spi->dev, "IRQ registration failed: %d\n", ret);
            goto remove_attribute;
        }
        dev_info(&spi->dev,
                 "IRQ observer ENABLED: count readable in device sysfs irq_events; no SPI or GPIO operations\n");
    } else {
        dev_info(&spi->dev, "IRQ observer disabled (default)\n");
    }
    return 0;

remove_attribute:
    device_remove_file(&spi->dev, &dev_attr_irq_events);
    return ret;
}

static void gxfp5a8b_remove(struct spi_device *spi)
{
    struct gxfp_probe_ctx *ctx = spi_get_drvdata(spi);

    device_remove_file(&spi->dev, &dev_attr_irq_events);
    dev_info(&spi->dev, "v0.3 probe removed; observed IRQ events=%lld\n",
             (long long)atomic64_read(&ctx->irq_events));
}

static const struct acpi_device_id gxfp_acpi_match[] = {
    { "GXFP5A8B", 0 }, { }
};
MODULE_DEVICE_TABLE(acpi, gxfp_acpi_match);

static const struct spi_device_id gxfp_spi_ids[] = {
    { "GXFP5A8B", 0 }, { }
};
MODULE_DEVICE_TABLE(spi, gxfp_spi_ids);

static struct spi_driver gxfp_driver = {
    .driver = {
        .name = "gxfp5a8b_probe",
        .acpi_match_table = gxfp_acpi_match,
    },
    .probe = gxfp5a8b_probe,
    .remove = gxfp5a8b_remove,
    .id_table = gxfp_spi_ids,
};
module_spi_driver(gxfp_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("GXFP5A8B ACPI/SPI diagnostic and optional IRQ counter v0.3");
MODULE_AUTHOR("Experimental reverse-engineering example");
