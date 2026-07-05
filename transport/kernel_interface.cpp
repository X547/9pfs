#include "Virtio9pDevice.h"
#include "Virtio9pHandle.h"

#include <new>


// Module glue for the virtio_9p transport driver. As in shmfs's
// kernel_interface.cpp, every module callback is a thin forwarder to a method
// on the owning class; no transport logic lives here.

device_manager_info *gDeviceManager = nullptr;


static float virtio_9p_supports_device(device_node *parent)
{
	return Virtio9pDevice::SupportsDevice(parent);
}


static status_t virtio_9p_register_device(device_node *parent)
{
	return Virtio9pDevice::RegisterDevice(parent);
}


static status_t virtio_9p_init_driver(device_node *node, void **cookie)
{
	return Virtio9pDevice::InitDriver(node, *(Virtio9pDevice**)cookie);
}


static void virtio_9p_uninit_driver(void *cookie)
{
	static_cast<Virtio9pDevice*>(cookie)->UninitDriver();
}


static status_t virtio_9p_register_child_devices(void *cookie)
{
	return static_cast<Virtio9pDevice*>(cookie)->RegisterChildDevices();
}


static status_t virtio_9p_init_device(void *driverCookie, void **cookie)
{
	Virtio9pDevice *device = static_cast<Virtio9pDevice*>(driverCookie);
	status_t status = device->InitDevice();
	if (status != B_OK)
		return status;
	*cookie = device;
	return B_OK;
}


static void virtio_9p_uninit_device(void *cookie)
{
	static_cast<Virtio9pDevice*>(cookie)->UninitDevice();
}


static status_t virtio_9p_open(void *deviceCookie, const char *path, int openMode,
	void **cookie)
{
	Virtio9pDevice *device = static_cast<Virtio9pDevice*>(deviceCookie);
	Virtio9pHandle *handle = new(std::nothrow) Virtio9pHandle(device);
	if (handle == nullptr)
		return B_NO_MEMORY;
	*cookie = handle;
	return B_OK;
}


static device_module_info sVirtio9pDeviceModule = {
	.info = {
		.name = VIRTIO_9P_DEVICE_MODULE_NAME,
	},
	.init_device = virtio_9p_init_device,
	.uninit_device = virtio_9p_uninit_device,
	.device_removed = NULL,
	.open = virtio_9p_open,
	.close = [](void *cookie) {
		return static_cast<Virtio9pHandle*>(cookie)->Close();
	},
	.free = [](void *cookie) {
		return static_cast<Virtio9pHandle*>(cookie)->Free();
	},
	.read = NULL,
	.write = NULL,
	.io = NULL,
	.control = [](void *cookie, uint32 op, void *buffer, size_t length) {
		return static_cast<Virtio9pHandle*>(cookie)->Ioctl(op, buffer, length);
	},
	.select = NULL,
	.deselect = NULL,
};


static driver_module_info sVirtio9pDriverModule = {
	.info = {
		.name = VIRTIO_9P_DRIVER_MODULE_NAME,
	},
	.supports_device = virtio_9p_supports_device,
	.register_device = virtio_9p_register_device,
	.init_driver = virtio_9p_init_driver,
	.uninit_driver = virtio_9p_uninit_driver,
	.register_child_devices = virtio_9p_register_child_devices,
};


_EXPORT module_dependency module_dependencies[] = {
	{ B_DEVICE_MANAGER_MODULE_NAME, (module_info**)&gDeviceManager },
	{ NULL }
};

_EXPORT module_info *modules[] = {
	(module_info*)&sVirtio9pDriverModule,
	(module_info*)&sVirtio9pDeviceModule,
	NULL
};
