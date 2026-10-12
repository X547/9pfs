#include "Virtio9pDevice.h"
#include "virtio_9p_defs.h"

#include <stdio.h>
#include <string.h>
#include <new>

#include <AutoDeleter.h>
#include <util/AutoLock.h>


#define ERROR(x...) dprintf("\33[33mvirtio_9p:\33[0m " x)

#if 0
#define TRACE(x...) dprintf("virtio_9p: " x)
#else
#define TRACE(x...) ;
#endif


extern device_manager_info *gDeviceManager;


// The transport DMA buffer holds one T-message region followed by one R-message
// region, each of msize bytes. Both regions are physically contiguous so each
// side of an exchange is a single virtqueue descriptor.
static const uint32 kMsize = 8192;

static const bigtime_t kReplyTimeout = 10 * 1000 * 1000;


static const char *
get_feature_name(uint64 feature)
{
	switch (feature) {
		case kVirtio9pFeatureMountTag:
			return "mount tag";
	}
	return nullptr;
}


//#pragma mark - Virtio9pDevice

Virtio9pDevice::Virtio9pDevice(device_node *node):
	fNode(node)
{
	fInterruptCondition.Init(this, "virtio_9p transfer");
}


Virtio9pDevice::~Virtio9pDevice()
{
	mutex_destroy(&fRequestLock);
}


void Virtio9pDevice::HandleQueueInterrupt()
{
	void *cookie = nullptr;
	uint32 usedLength = 0;
	while (fVirtio->queue_dequeue(fRequestQueue, &cookie, &usedLength)) {
		if ((int32)(addr_t)cookie == atomic_get(&fCurrentRequest)) {
			fReplyLength = usedLength;
			fReplyReady = true;
		}
		atomic_set(&fInFlight, 0);
		fInterruptCondition.NotifyAll();
	}
}


//#pragma mark - driver module API

float Virtio9pDevice::SupportsDevice(device_node *parent)
{
	const char *bus;
	uint16 deviceType;

	if (gDeviceManager->get_attr_string(parent, B_DEVICE_BUS, &bus, false) != B_OK)
		return -1;
	if (strcmp(bus, "virtio") != 0)
		return 0.0;

	if (gDeviceManager->get_attr_uint16(parent, VIRTIO_DEVICE_TYPE_ITEM,
			&deviceType, true) != B_OK || deviceType != VIRTIO_DEVICE_ID_9P) {
		return 0.0;
	}

	TRACE("Virtio 9P device found!\n");
	return 0.6;
}


status_t Virtio9pDevice::RegisterDevice(device_node *parent)
{
	device_attr attrs[] = {
		{ B_DEVICE_PRETTY_NAME, B_STRING_TYPE, {.string = "Virtio 9P"} },
		{ NULL }
	};

	return gDeviceManager->register_node(parent, VIRTIO_9P_DRIVER_MODULE_NAME,
		attrs, NULL, NULL);
}


status_t Virtio9pDevice::InitDriver(device_node *node, Virtio9pDevice *&outDevice)
{
	ObjectDeleter<Virtio9pDevice> device(new(std::nothrow) Virtio9pDevice(node));
	if (!device.IsSet())
		return B_NO_MEMORY;

	// Allocate the contiguous DMA command buffer (T region + R region).
	uint8 *bufferAddr = nullptr;
	area_id area = create_area("virtio_9p command buffer", (void**)&bufferAddr,
		B_ANY_KERNEL_ADDRESS, 2 * kMsize, B_CONTIGUOUS,
		B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA);
	if (area < B_OK)
		return area;

	physical_entry entry;
	status_t status = get_memory_map(bufferAddr, 2 * kMsize, &entry, 1);
	if (status != B_OK) {
		delete_area(area);
		return status;
	}

	device->fBufferArea = area;
	device->fBufferAddr = bufferAddr;
	device->fBufferPhysAddr = entry.address;
	device->fMsize = kMsize;

	outDevice = device.Detach();
	return B_OK;
}


void Virtio9pDevice::UninitDriver()
{
	if (fBufferArea >= 0)
		delete_area(fBufferArea);
	delete this;
}


status_t Virtio9pDevice::RegisterChildDevices()
{
	int32 id = gDeviceManager->create_id(VIRTIO_9P_DEVICE_ID_GENERATOR);
	if (id < 0)
		return id;

	char name[64];
	snprintf(name, sizeof(name), "misc/virtio_9p/%" B_PRId32, id);

	return gDeviceManager->publish_device(fNode, name, VIRTIO_9P_DEVICE_MODULE_NAME);
}


//#pragma mark - device init

status_t Virtio9pDevice::InitDevice()
{
	device_node *parent = gDeviceManager->get_parent_node(fNode);
	gDeviceManager->get_driver(parent, (driver_module_info**)&fVirtio,
		(void**)&fVirtioDevice);
	gDeviceManager->put_node(parent);

	fVirtio->negotiate_features(fVirtioDevice,
		kVirtio9pFeatureMountTag | VIRTIO_FEATURE_RING_INDIRECT_DESC,
		&fFeatures, &get_feature_name);

	if ((fFeatures & kVirtio9pFeatureMountTag) != 0) {
		uint16 tagLen = 0;
		status_t status = fVirtio->read_device_config(fVirtioDevice,
			kVirtio9pConfigTagLenOffset, &tagLen, sizeof(tagLen));
		if (status == B_OK && tagLen > 0) {
			if (tagLen >= kNineMaxTagLen)
				tagLen = kNineMaxTagLen - 1;
			fVirtio->read_device_config(fVirtioDevice, kVirtio9pConfigTagOffset,
				fMountTag, tagLen);
			fMountTag[tagLen] = '\0';
		}
	}

	TRACE("mount tag \"%s\", msize %" B_PRIu32 "\n", fMountTag, fMsize);

	uint16 requestedSize = 0;
	status_t status = fVirtio->alloc_queues(fVirtioDevice, 1, &fRequestQueue,
		&requestedSize);
	if (status != B_OK) {
		ERROR("queue allocation failed (%s)\n", strerror(status));
		return status;
	}

	status = fVirtio->setup_interrupt(fVirtioDevice,
		[](void *driverCookie) {}, this);
	if (status == B_OK) {
		status = fVirtio->queue_setup_interrupt(fRequestQueue,
			[](void *driverCookie, void *cookie) {
				static_cast<Virtio9pDevice*>(cookie)->HandleQueueInterrupt();
			}, this);
	}

	return status;
}


void Virtio9pDevice::UninitDevice()
{
	// Freeing the queues resets the device, so that the queues the next
	// InitDevice() sets up start where the device does.
	if (fVirtio != nullptr && fVirtioDevice != nullptr) {
		fVirtio->free_interrupts(fVirtioDevice);
		fVirtio->free_queues(fVirtioDevice);
	}
	fRequestQueue = nullptr;
	// a request the device still held went with the reset
	atomic_set(&fInFlight, 0);
}


//#pragma mark - transport

status_t Virtio9pDevice::GetInfo(NineTransportInfo &info)
{
	info.msize = fMsize;
	strlcpy(info.mountTag, fMountTag, sizeof(info.mountTag));
	return B_OK;
}


status_t Virtio9pDevice::Transceive(const void *tbuf, size_t tlen, void *rbuf,
	size_t rcap, size_t &rlen)
{
	if (tlen > fMsize || rcap > fMsize)
		return B_BAD_VALUE;

	MutexLocker locker(&fRequestLock);

	// Until the device returns a request given up on, the buffer is its own;
	// writing the next request there would mix the two.
	if (atomic_get(&fInFlight) != 0) {
		ConditionVariableEntry entry;
		fInterruptCondition.Add(&entry);
		if (atomic_get(&fInFlight) != 0)
			entry.Wait(B_RELATIVE_TIMEOUT, kReplyTimeout);
		if (atomic_get(&fInFlight) != 0)
			return B_BUSY;
	}

	memcpy(fBufferAddr, tbuf, tlen);

	physical_entry entries[2];
	entries[0].address = fBufferPhysAddr;
	entries[0].size = tlen;
	entries[1].address = fBufferPhysAddr + fMsize;
	entries[1].size = rcap;

	fReplyReady = false;
	fReplyLength = 0;
	int32 request = atomic_add(&fCurrentRequest, 1) + 1;

	ConditionVariableEntry conditionEntry;
	fInterruptCondition.Add(&conditionEntry);

	atomic_set(&fInFlight, 1);
	status_t status = fVirtio->queue_request_v(fRequestQueue, entries, 1, 1,
		(void*)(addr_t)request);
	if (status != B_OK) {
		atomic_set(&fInFlight, 0);
		return status;
	}

	status = conditionEntry.Wait(B_RELATIVE_TIMEOUT, kReplyTimeout);
	if (!fReplyReady)
		return status == B_TIMED_OUT ? B_TIMED_OUT : B_IO_ERROR;

	uint32 usedLength = fReplyLength;
	if (usedLength > rcap)
		usedLength = rcap;
	memcpy(rbuf, fBufferAddr + fMsize, usedLength);
	rlen = usedLength;

	return B_OK;
}
