/* SPDX-License-Identifier: GPL-2.0-only */
#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdio.h>

#include "mock_linux.h"
#include "gx_spi_linux.h"

const char mock_client_handle, mock_source_handle;
bool mock_fail_alloc, mock_fail_spi_ref;
atomic_uint mock_admissions;
static const char mock_spi_class;

struct fixture {
	struct acpi_device source, client;
	struct acpi_device_physical_node node, controller_nodes[3];
	struct software_node properties;
	struct mfd_cell cell;
	struct pci_dev pci;
	struct platform_device host;
	struct spi_controller controller;
	struct spi_device spi;
};

static void init_fixture(struct fixture *f)
{
	memset(f, 0, sizeof(*f));
	f->source.handle = &mock_source_handle;
	assert(!pthread_mutex_init(&f->source.physical_node_lock, NULL));
	f->source.physical_node_list = &f->controller_nodes[0];
	f->controller_nodes[0].dev = &f->pci.dev;
	f->controller_nodes[0].next = &f->controller_nodes[1];
	f->controller_nodes[1].dev = &f->host.dev;
	f->controller_nodes[1].next = &f->controller_nodes[2];
	f->controller_nodes[2].dev = &f->controller.dev;
	f->controller.dev.class = &mock_spi_class;
	f->client.handle = &mock_client_handle;
	f->client.parent = &f->source;
	f->client.hid = "GXFP5A8B";
	f->client.enumerated = true;
	f->client.physical_node_count = 1;
	f->client.physical_node_list = &f->node;
	assert(!pthread_mutex_init(&f->client.physical_node_lock, NULL));
	f->node.dev = &f->spi.dev;

	f->properties.type = LPSS_CNL_SSP;
	f->cell.name = "pxa2xx-spi";
	f->cell.swnode = &f->properties;
	f->pci.dev.kind = MOCK_PCI;
	f->pci.dev.companion = &f->source;
	f->pci.vendor = PCI_VENDOR_ID_INTEL;
	f->pci.device = 0x34ab;
	f->host.dev.kind = MOCK_PLATFORM;
	f->host.dev.companion = &f->source;
	f->host.dev.parent = &f->pci.dev;
	f->host.cell = &f->cell;
	f->controller.dev.parent = &f->host.dev;
	f->spi.dev.parent = &f->controller.dev;
	f->spi.dev.companion = &f->client;
	f->spi.controller = &f->controller;
	f->spi.num_chipselect = 1;
	f->spi.cs_index_mask = 1;
	f->spi.bits_per_word = 8;
	f->spi.max_speed_hz = 10000000;
	f->spi.refs = 1;
}

static void finish_fixture(struct fixture *f)
{
	assert(!f->spi.dev.drvdata && f->spi.refs == 1);
	assert(!pthread_mutex_destroy(&f->client.physical_node_lock));
	assert(!pthread_mutex_destroy(&f->source.physical_node_lock));
}

static void test_identity(void)
{
	struct device rival = { .class = &mock_spi_class };
	struct device dma = { .kind = MOCK_PLATFORM };
	struct acpi_device_physical_node competing = { .dev = &rival };
	struct acpi_device_physical_node dma_node = { .dev = &dma };
	struct fixture f;

	init_fixture(&f);
	assert(!gx_spi_linux_check_client(&f.spi));

	f.controller_nodes[2].next = &dma_node;
	assert(!gx_spi_linux_check_client(&f.spi));
	dma_node.next = &competing;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.controller_nodes[2].next = NULL;
	f.client.physical_node_count = 2;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.client.physical_node_count = 1;
	f.node.dev = &f.host.dev;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.node.dev = &f.spi.dev;
	f.client.enumerated = false;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.client.enumerated = true;
	f.client.handle = &mock_source_handle;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.client.handle = &mock_client_handle;
	f.host.dev.companion = &f.client;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.host.dev.companion = &f.source;
	f.pci.dev.companion = NULL;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.pci.dev.companion = &f.source;
	f.pci.device = 0x02ab;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.pci.device = 0x34ab;
	f.properties.type = 0;
	assert(gx_spi_linux_check_client(&f.spi) == -ENODEV);
	f.properties.type = LPSS_CNL_SSP;
	f.spi.cs_gpio = &f;
	assert(gx_spi_linux_check_client(&f.spi) == -EINVAL);
	f.spi.cs_gpio = NULL;
	f.spi.mode = 4;
	assert(gx_spi_linux_check_client(&f.spi) == -EINVAL);
	f.spi.mode = 0;
	f.spi.num_chipselect = 2;
	assert(gx_spi_linux_check_client(&f.spi) == -EINVAL);
	finish_fixture(&f);
}

static void test_probe_and_gate(void)
{
	struct gx_spi_result result;
	struct fixture f;

	init_fixture(&f);
	mock_fail_alloc = true;
	assert(gx_spi_linux_probe(&f.spi) == -ENOMEM);
	mock_fail_alloc = false;
	mock_fail_spi_ref = true;
	assert(gx_spi_linux_probe(&f.spi) == -ENODEV);
	mock_fail_spi_ref = false;
	assert(!f.spi.dev.drvdata && f.spi.refs == 1);
	assert(!gx_spi_linux_probe(&f.spi));
	assert(gx_spi_linux_probe(&f.spi) == -EBUSY);
	assert(f.spi.refs == 2);
	assert(gx_spi_linux_read_cpu(&f.spi, 1000, &result) == -EOPNOTSUPP);
	assert(result.count == 1 && result.receipts[0].status == -EOPNOTSUPP);
	assert(!gx_spi_linux_cancel(&f.spi));
	gx_spi_linux_remove(&f.spi);
	assert(gx_spi_linux_read_cpu(&f.spi, 1000, &result) == -ENODEV);
	assert(!result.count && !gx_spi_linux_cancel(&f.spi));
	gx_spi_linux_remove(&f.spi);
	assert(!gx_spi_linux_probe(&f.spi));
	gx_spi_linux_remove(&f.spi);
	finish_fixture(&f);
}

static _Atomic(struct gx_spi_session *) active_session;
static atomic_bool block_executor, release_executor, remove_returned;

int __real_gx_spi_linux_execute(void *context, const struct gx_spi_message *message,
				struct gx_spi_session *session,
				struct gx_spi_receipt *receipt);

static void pause_briefly(void)
{
	const struct timespec delay = { .tv_nsec = 1000000 };

	nanosleep(&delay, NULL);
}

int __wrap_gx_spi_linux_execute(void *context, const struct gx_spi_message *message,
				struct gx_spi_session *session,
				struct gx_spi_receipt *receipt)
{
	if (!atomic_load(&block_executor))
		return __real_gx_spi_linux_execute(context, message, session, receipt);

	atomic_store(&active_session, session);
	for (unsigned int i = 0; i < 5000 && !atomic_load(&release_executor); i++)
		pause_briefly();
	assert(atomic_load(&release_executor));
	return gx_spi_stop_status(session);
}

struct read_worker {
	struct spi_device *spi;
	int status;
};

static void *read_thread(void *data)
{
	struct read_worker *worker = data;
	struct gx_spi_result result;

	worker->status = gx_spi_linux_read_cpu(worker->spi, 60000, &result);
	return NULL;
}

static void *remove_thread(void *data)
{
	gx_spi_linux_remove(data);
	atomic_store(&remove_returned, true);
	return NULL;
}

static void test_cancel_and_drain(void)
{
	struct fixture f;
	struct read_worker worker = { .spi = &f.spi }, queued = { .spi = &f.spi };
	struct gx_spi_result rejected;
	struct gx_spi_session *session;
	pthread_t reader, second_reader, remover;

	init_fixture(&f);
	assert(!gx_spi_linux_probe(&f.spi));
	atomic_store(&block_executor, true);
	atomic_store(&mock_admissions, 0);
	assert(!pthread_create(&reader, NULL, read_thread, &worker));
	for (unsigned int i = 0; i < 5000 && !atomic_load(&active_session); i++)
		pause_briefly();
	session = atomic_load(&active_session);
	assert(session);
	assert(gx_spi_linux_cancel(&f.spi));
	assert(gx_spi_stop_status(session) == -ECANCELED);

	assert(!pthread_create(&second_reader, NULL, read_thread, &queued));
	for (unsigned int i = 0; i < 5000 && atomic_load(&mock_admissions) < 2; i++)
		pause_briefly();
	assert(atomic_load(&mock_admissions) == 2);

	assert(!pthread_create(&remover, NULL, remove_thread, &f.spi));
	for (unsigned int i = 0; i < 5000 &&
	     gx_spi_stop_status(session) != -ENODEV; i++)
		pause_briefly();
	assert(gx_spi_stop_status(session) == -ENODEV);
	assert(!gx_spi_linux_cancel(&f.spi));
	assert(!atomic_load(&remove_returned));
	assert(gx_spi_linux_read_cpu(&f.spi, 1000, &rejected) == -ENODEV);
	assert(!rejected.count);
	atomic_store(&release_executor, true);
	assert(!pthread_join(reader, NULL));
	assert(!pthread_join(second_reader, NULL));
	assert(!pthread_join(remover, NULL));
	assert(queued.status == -ENODEV);
	assert(worker.status == -ENODEV && atomic_load(&remove_returned));
	finish_fixture(&f);
}

int main(void)
{
	test_identity();
	test_probe_and_gate();
	test_cancel_and_drain();
	puts("3 SPI Linux identity and lifecycle tests passed");
	return 0;
}
