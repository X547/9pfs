#pragma once

#include <device_manager.h>
#include <condition_variable.h>
#include <lock.h>
#include <virtio.h>

#include "NineTransport.h"


#define VIRTIO_9P_DEVICE_MODULE_NAME "drivers/misc/virtio_9p/device_v1"
#define VIRTIO_9P_DRIVER_MODULE_NAME "drivers/misc/virtio_9p/driver_v1"
#define VIRTIO_9P_DEVICE_ID_GENERATOR "virtio_9p/device_id"


// Owns one VirtIO 9P device: the request virtqueue, a contiguous DMA command
// buffer, and the mount tag / msize read from config space. Exposes a single
// synchronous transport primitive, Transceive(), that carries one 9P message
// exchange over the queue. Access is serialized by fRequestLock, matching the
// one-in-flight model of the virtio_block driver.

class Virtio9pDevice {
private:
	device_node *fNode {};
	virtio_device_interface *fVirtio {};
	::virtio_device fVirtioDevice {};
	::virtio_queue fRequestQueue {};
	uint64 fFeatures = 0;

	area_id fBufferArea = -1;
	uint8 *fBufferAddr = nullptr;
	phys_addr_t fBufferPhysAddr = 0;
	uint32 fMsize = 0;

	char fMountTag[kNineMaxTagLen] {};

	mutex fRequestLock = MUTEX_INITIALIZER("virtio_9p request");
	ConditionVariable fInterruptCondition;
	int32 fCurrentRequest = 0;
	uint32 fReplyLength = 0;
	bool fReplyReady = false;

	void HandleQueueInterrupt();

public:
	Virtio9pDevice(device_node *node);
	~Virtio9pDevice();

	static float SupportsDevice(device_node *parent);
	static status_t RegisterDevice(device_node *parent);
	static status_t InitDriver(device_node *node, Virtio9pDevice *&outDevice);
	void UninitDriver();
	status_t RegisterChildDevices();

	status_t InitDevice();
	void UninitDevice();

	status_t GetInfo(NineTransportInfo &info);
	status_t Transceive(const void *tbuf, size_t tlen, void *rbuf, size_t rcap, size_t &rlen);
};
