// SPDX-License-Identifier: GPL-2.0-only
/*
 * Goodix GXFP5A8B: passive ACPI/SPI probe, optional IRQ counter and two mutually exclusive opt-in CPU reads (v0.5)
 *
 * observe_irq=0 (default): identical observation-only intent to v0.2.
 * observe_irq=1: request existing spi->irq and count events.
 * allow_cpu_read=0 (default): no SPI transfer, no GPIO activity.
 * allow_cpu_read=1: opt in to the original v0.4 diagnostic without wakeup.
 * allow_wakeup_cpu_read=1: opt in to one deliberate E5 raw write, wait >=50ms,
 * then F0 00 A8 + F1/read 2 bytes. Triggered ONLY by an explicit root read
 * of wakeup_cpu_version. No I/O when module loads. Neither option is on by
 * default; only one may be armed. No reset, firmware, GPIO, TLS, PSK or MCU
 * 0x82 operation. Hardware safety of E5 is not established experimentally.
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
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

static bool observe_irq;
module_param(observe_irq, bool, 0444);
MODULE_PARM_DESC(observe_irq,
                 "Enable existing SPI IRQ for event counting (default: false)");

static bool allow_cpu_read;
module_param(allow_cpu_read, bool, 0444);
MODULE_PARM_DESC(allow_cpu_read,
                 "Enable one explicit, root-only diagnostic SPI read (default: false)");

static bool allow_wakeup_cpu_read;
module_param(allow_wakeup_cpu_read, bool, 0444);
MODULE_PARM_DESC(allow_wakeup_cpu_read,
                 "One explicit E5 raw wake and CPU read, root-only (default: false)");

struct gxfp_probe_ctx {
    atomic64_t irq_events;
    atomic_t cpu_read_attempted;
    struct mutex spi_lock;
    struct spi_device *spi;
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

/*
 * These sysfs reads are hardware operations, not cached information.
 * They exist only with explicit module-load opt-in and are root-only (0400).
 * Each module load permits at most one attempt, whether it succeeds or fails.
 * Never execute SPI automatically at bind/open/probe/resume.
 */
static ssize_t gxfp_read_cpu_version(struct device *dev, char *buf,
                                     bool with_wakeup)
{
    struct gxfp_probe_ctx *ctx = dev_get_drvdata(dev);
    const u8 select_addr[3] = { 0xf0, 0x00, 0xa8 };
    const u8 cmd = 0xf1;
    const u8 wake = 0xe5;
    u8 reply[2] = { 0, 0 };
    const char *phase = "initial";
    int ret;

    if (!ctx || (with_wakeup ? !allow_wakeup_cpu_read : !allow_cpu_read))
        return -EACCES;

    /* Fail closed unless the known LG Gram ACPI SPI profile is applied. */
    if (ctx->spi->controller->bus_num != 0 ||
        spi_get_chipselect(ctx->spi, 0) != 0 ||
        ctx->spi->max_speed_hz != 10000000 ||
        ctx->spi->bits_per_word != 8 ||
        ctx->spi->mode != SPI_MODE_0)
        return -EINVAL;

    if (atomic_cmpxchg(&ctx->cpu_read_attempted, 0, 1) != 0)
        return -EALREADY;

    /* Serialize all steps with other operations from this experimental driver. */
    mutex_lock(&ctx->spi_lock);
    if (with_wakeup) {
        phase = "raw E5";
        ret = spi_write(ctx->spi, &wake, sizeof(wake));
        if (ret)
            goto out_unlock;
        /* Windows WakeUp waits 50 ms AFTER successful host-side E5 send. */
        usleep_range(50000, 55000);
    }

    phase = "F0 00 A8 address selector";
    ret = spi_write(ctx->spi, select_addr, sizeof(select_addr));
    if (ret)
        goto out_unlock;

    phase = "F1 read 2 bytes";
    ret = spi_write_then_read(ctx->spi, &cmd, sizeof(cmd),
                              reply, sizeof(reply));
out_unlock:
    mutex_unlock(&ctx->spi_lock);
    if (ret) {
        dev_warn(dev, "CPU diagnostic failed during %s: %d (no retry this load)\n",
                 phase, ret);
        return ret;
    }

    /* Returned word is NOT a validated CPU version or proof of MCU response. */
    return sysfs_emit(buf,
                      "wakeup=%u raw=%02x %02x be16=0x%04x status=unvalidated\n",
                      with_wakeup, reply[0], reply[1],
                      ((unsigned int)reply[0] << 8) | reply[1]);
}

static ssize_t cpu_version_show(struct device *dev,
                                struct device_attribute *attr, char *buf)
{
    return gxfp_read_cpu_version(dev, buf, false);
}
static DEVICE_ATTR(cpu_version, 0400, cpu_version_show, NULL);

static ssize_t wakeup_cpu_version_show(struct device *dev,
                                       struct device_attribute *attr, char *buf)
{
    return gxfp_read_cpu_version(dev, buf, true);
}
static DEVICE_ATTR(wakeup_cpu_version, 0400, wakeup_cpu_version_show, NULL);

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
    atomic_set(&ctx->cpu_read_attempted, 0);
    mutex_init(&ctx->spi_lock);
    ctx->spi = spi;
    spi_set_drvdata(spi, ctx);

    if ((allow_cpu_read && allow_wakeup_cpu_read) ||
        (observe_irq && (allow_cpu_read || allow_wakeup_cpu_read)))
        return dev_err_probe(&spi->dev, -EINVAL,
                             "choose only one active option: IRQ, direct CPU read, or E5+CPU read\n");

    dev_info(&spi->dev,
             "v0.5 probe: bus=%d cs=%u mode=0x%x bits=%u max_hz=%u irq=%d observe_irq=%d cpu_read_optin=%d wakeup_cpu_optin=%d\n",
             spi->controller->bus_num, spi_get_chipselect(spi, 0),
             spi->mode, spi->bits_per_word, spi->max_speed_hz,
             spi->irq, observe_irq, allow_cpu_read, allow_wakeup_cpu_read);

    if (handle) {
        status = acpi_walk_resources(handle, "_CRS", gxfp_resource_cb, spi);
        if (ACPI_FAILURE(status))
            dev_warn(&spi->dev, "_CRS inspection failed: %s\n",
                     acpi_format_exception(status));
    }

    ret = device_create_file(&spi->dev, &dev_attr_irq_events);
    if (ret)
        return dev_err_probe(&spi->dev, ret, "could not create read-only event counter\n");

    if (allow_cpu_read) {
        ret = device_create_file(&spi->dev, &dev_attr_cpu_version);
        if (ret) {
            dev_err(&spi->dev, "could not create root-only cpu_version file: %d\n", ret);
            goto remove_attribute;
        }
        dev_warn(&spi->dev,
                 "CPU SPI read ARMED (not executed): root must explicitly read cpu_version; one attempt only\n");
    }

    if (allow_wakeup_cpu_read) {
        ret = device_create_file(&spi->dev, &dev_attr_wakeup_cpu_version);
        if (ret) {
            dev_err(&spi->dev, "could not create wakeup_cpu_version: %d\n", ret);
            goto remove_attribute;
        }
        dev_warn(&spi->dev,
                 "E5+CPU diagnostic ARMED only; root must read wakeup_cpu_version to transmit; one attempt\n");
    }

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
    if (allow_wakeup_cpu_read)
        device_remove_file(&spi->dev, &dev_attr_wakeup_cpu_version);
    if (allow_cpu_read)
        device_remove_file(&spi->dev, &dev_attr_cpu_version);
    device_remove_file(&spi->dev, &dev_attr_irq_events);
    return ret;
}

static void gxfp5a8b_remove(struct spi_device *spi)
{
    struct gxfp_probe_ctx *ctx = spi_get_drvdata(spi);

    if (allow_wakeup_cpu_read)
        device_remove_file(&spi->dev, &dev_attr_wakeup_cpu_version);
    if (allow_cpu_read)
        device_remove_file(&spi->dev, &dev_attr_cpu_version);
    device_remove_file(&spi->dev, &dev_attr_irq_events);
    dev_info(&spi->dev, "v0.5 probe removed; observed IRQ events=%lld\n",
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
MODULE_DESCRIPTION("GXFP5A8B SPI diagnostic, one-shot opt-in E5 plus CPU read v0.5");
MODULE_AUTHOR("Experimental reverse-engineering example");
