/* SPDX-License-Identifier: GPL-2.0-only */
#include <linux/completion.h>
#include <linux/refcount.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>

#include "gx_spi_linux.h"

struct gx_spi_device {
	struct spi_device *spi;
	struct gx_spi_session session;
	refcount_t users;
	struct completion drained;
};

/* Protect publication and caller admission, never an operation or drain. */
static DEFINE_MUTEX(client_lock);

static void put_client(struct gx_spi_device *client)
{
	if (refcount_dec_and_test(&client->users))
		complete(&client->drained);
}

int gx_spi_linux_probe(struct spi_device *spi)
{
	struct gx_spi_device *client;
	int ret;

	ret = gx_spi_linux_check_client(spi);
	if (ret)
		return ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;
	client->spi = spi_dev_get(spi);
	if (!client->spi) {
		ret = -ENODEV;
		goto free_client;
	}
	ret = gx_spi_session_init(&client->session, gx_spi_linux_execute,
				  client->spi);
	if (ret)
		goto put_spi;

	refcount_set(&client->users, 1);
	init_completion(&client->drained);

	mutex_lock(&client_lock);
	if (spi_get_drvdata(spi))
		ret = -EBUSY;
	else
		spi_set_drvdata(spi, client);
	mutex_unlock(&client_lock);
	if (!ret)
		return 0;

put_spi:
	spi_dev_put(client->spi);
free_client:
	kfree(client);
	return ret;
}

int gx_spi_linux_read_cpu(struct spi_device *spi, unsigned int timeout_ms,
			  struct gx_spi_result *result)
{
	struct gx_spi_device *client;
	int ret;

	if (!result)
		return -EINVAL;
	memset(result, 0, sizeof(*result));

	mutex_lock(&client_lock);
	client = spi_get_drvdata(spi);
	if (client)
		refcount_inc(&client->users);
	mutex_unlock(&client_lock);
	if (!client)
		return -ENODEV;

	ret = gx_spi_read_cpu(&client->session, timeout_ms, result);
	put_client(client);
	return ret;
}

bool gx_spi_linux_cancel(struct spi_device *spi)
{
	struct gx_spi_device *client;
	bool cancelled;

	mutex_lock(&client_lock);
	client = spi_get_drvdata(spi);
	cancelled = client && gx_spi_cancel(&client->session);
	mutex_unlock(&client_lock);
	return cancelled;
}

void gx_spi_linux_remove(struct spi_device *spi)
{
	struct gx_spi_device *client;

	mutex_lock(&client_lock);
	client = spi_get_drvdata(spi);
	spi_set_drvdata(spi, NULL);
	mutex_unlock(&client_lock);
	if (!client)
		return;

	/* Reject new callers, stop the session and drain admitted callers. */
	gx_spi_remove(&client->session);
	put_client(client);
	wait_for_completion(&client->drained);
	spi_dev_put(client->spi);
	kfree(client);
}
