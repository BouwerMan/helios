#include <drivers/device.h>
#include <stdlib.h>
#include <util/list.h>
#include <util/log.h>

LIST_HEAD(g_registered_devices);

void register_device(const char* name, struct file_ops* fops)
{
	struct device* dev = kzmalloc(sizeof(struct device));
	if (!dev) {
		log_error("Failed to allocate device");
		return;
	}

	strncpy(dev->name, name, DEVICE_MAX_NAME - 1);

	dev->fops = fops;

	list_add(&g_registered_devices, &dev->list);
}
