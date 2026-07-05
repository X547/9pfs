#pragma once

#include "Virtio9pDevice.h"


// Per-open cookie for a published /dev/misc/virtio_9p/N node. Dispatches the
// transport ioctls to the owning Virtio9pDevice.

class Virtio9pHandle {
private:
	Virtio9pDevice *fDevice {};

public:
	Virtio9pHandle(Virtio9pDevice *device): fDevice(device) {}

	status_t Close();
	status_t Free();
	status_t Ioctl(uint32 op, void *buffer, size_t length);
};
