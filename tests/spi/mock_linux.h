/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef GX_SPI_MOCK_LINUX_H
#define GX_SPI_MOCK_LINUX_H

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef uint32_t u32;
typedef const void *acpi_handle;
typedef atomic_uint refcount_t;

struct device {
	struct device *parent;
	struct acpi_device *companion;
	void *drvdata;
	const void *class;
	enum { MOCK_SPI, MOCK_PLATFORM, MOCK_PCI } kind;
};

struct acpi_device_physical_node {
	struct device *dev;
	struct acpi_device_physical_node *next;
};

struct acpi_device {
	acpi_handle handle;
	struct acpi_device *parent;
	const char *hid;
	bool enumerated;
	unsigned int physical_node_count;
	struct acpi_device_physical_node *physical_node_list;
	pthread_mutex_t physical_node_lock;
};

struct software_node {
	u32 type;
};

struct mfd_cell {
	const char *name;
	const struct software_node *swnode;
};

struct platform_device {
	struct device dev;
	const struct mfd_cell *cell;
};

struct pci_dev {
	struct device dev;
	unsigned int vendor, device;
};

struct spi_controller {
	struct device dev;
};

struct spi_device {
	struct device dev;
	struct spi_controller *controller;
	unsigned int num_chipselect, cs_index_mask, mode, bits_per_word, max_speed_hz;
	unsigned int cs, refs;
	void *cs_gpio;
};

struct completion {
	pthread_mutex_t lock;
	pthread_cond_t ready;
	bool done;
};

#define CONFIG_ACPI 1
#define GFP_KERNEL 0
#define SPI_MODE_0 0
#define PCI_VENDOR_ID_INTEL 0x8086
#define LPSS_CNL_SSP 16
#define ACPI_COMPANION(dev) ((dev)->companion)
#define ACPI_FAILURE(status) ((status) != 0)
#define acpi_device_hid(adev) ((adev)->hid)
#define acpi_device_enumerated(adev) ((adev)->enumerated)
#define acpi_dev_parent(adev) ((adev)->parent)
#define list_first_entry_or_null(head, type, member) (*(head))
#define list_for_each_entry(entry, head, member) \
	for ((entry) = *(head); (entry); (entry) = (entry)->next)
#define DEFINE_MUTEX(name) pthread_mutex_t name = PTHREAD_MUTEX_INITIALIZER
#define mutex_lock pthread_mutex_lock
#define mutex_unlock pthread_mutex_unlock
#define dev_is_pci(dev) ((dev)->kind == MOCK_PCI)
#define dev_is_platform(dev) ((dev)->kind == MOCK_PLATFORM)
#define to_pci_dev(dev) ((struct pci_dev *)(dev))
#define to_platform_device(dev) ((struct platform_device *)(dev))
#define mfd_get_cell(pdev) ((pdev)->cell)
#define software_node_fwnode(node) (node)
#define spi_get_chipselect(spi, index) ((spi)->cs)
#define spi_get_csgpiod(spi, index) ((spi)->cs_gpio)
#define spi_get_drvdata(spi) ((spi)->dev.drvdata)
#define spi_set_drvdata(spi, ptr) ((spi)->dev.drvdata = (ptr))
#define refcount_set(refs, count) atomic_init(refs, count)
extern atomic_uint mock_admissions;

static inline void refcount_inc(refcount_t *refs)
{
	atomic_fetch_add(refs, 1);
	atomic_fetch_add(&mock_admissions, 1);
}
#define refcount_dec_and_test(refs) (atomic_fetch_sub(refs, 1) == 1)
#define kfree free

extern const char mock_client_handle, mock_source_handle;
extern bool mock_fail_alloc, mock_fail_spi_ref;

static inline int acpi_get_handle(acpi_handle scope, const char *path,
				  acpi_handle *handle)
{
	assert(!scope);
	if (!strcmp(path, "\\_SB_.PCI0.SPI1.FPNT"))
		*handle = &mock_client_handle;
	else if (!strcmp(path, "\\_SB_.PCI0.SPI1"))
		*handle = &mock_source_handle;
	else
		return -ENODEV;
	return 0;
}

static inline bool device_match_acpi_handle(struct device *dev, acpi_handle handle)
{
	return dev->companion && dev->companion->handle == handle;
}

static inline int fwnode_property_read_u32(const struct software_node *node,
					  const char *property, u32 *type)
{
	assert(!strcmp(property, "intel,spi-pxa2xx-type"));
	*type = node->type;
	return 0;
}

static inline void *kzalloc(size_t size, int flags)
{
	(void)flags;
	return mock_fail_alloc ? NULL : calloc(1, size);
}

static inline struct spi_device *spi_dev_get(struct spi_device *spi)
{
	if (mock_fail_spi_ref)
		return NULL;
	spi->refs++;
	return spi;
}

static inline void spi_dev_put(struct spi_device *spi)
{
	assert(spi->refs > 1);
	spi->refs--;
}

static inline void init_completion(struct completion *completion)
{
	assert(!pthread_mutex_init(&completion->lock, NULL));
	assert(!pthread_cond_init(&completion->ready, NULL));
	completion->done = false;
}

static inline void complete(struct completion *completion)
{
	pthread_mutex_lock(&completion->lock);
	completion->done = true;
	pthread_cond_signal(&completion->ready);
	pthread_mutex_unlock(&completion->lock);
}

static inline void wait_for_completion(struct completion *completion)
{
	pthread_mutex_lock(&completion->lock);
	while (!completion->done)
		pthread_cond_wait(&completion->ready, &completion->lock);
	pthread_mutex_unlock(&completion->lock);
	pthread_cond_destroy(&completion->ready);
	pthread_mutex_destroy(&completion->lock);
}

/* The physical branch must be eliminated. These functions have no definitions. */
struct spi_transfer {
	unsigned int len;
	const void *tx_buf;
	void *rx_buf;
};

struct spi_message {
	unsigned int actual_length;
};

size_t spi_max_message_size(struct spi_device *spi);
size_t spi_max_transfer_size(struct spi_device *spi);
void *kmalloc(size_t size, int flags);
void spi_message_init(struct spi_message *msg);
void spi_message_add_tail(struct spi_transfer *transfer, struct spi_message *msg);
int spi_sync(struct spi_device *spi, struct spi_message *msg);

#endif
