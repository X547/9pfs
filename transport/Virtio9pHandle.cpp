#include "Virtio9pHandle.h"

#include <string.h>

#include <kernel.h>


// The transport node is opened and driven only by the 9pfs file system, which
// runs in the kernel, so ioctl buffers are kernel addresses and are used
// directly. A userland caller passes user addresses and is refused: taking its
// pointers would let it make the kernel read or write any memory.

static bool is_kernel_range(const void *address, size_t size)
{
	addr_t start = (addr_t)address;
	addr_t end = start + size;
	return address != nullptr && end >= start && IS_KERNEL_ADDRESS(start)
		&& (size == 0 || IS_KERNEL_ADDRESS(end - 1));
}

status_t Virtio9pHandle::Close()
{
	return B_OK;
}


status_t Virtio9pHandle::Free()
{
	delete this;
	return B_OK;
}


status_t Virtio9pHandle::Ioctl(uint32 op, void *buffer, size_t length)
{
	switch (op) {
		case kIoctl9pGetInfo: {
			if (length < sizeof(NineTransportInfo))
				return B_BAD_VALUE;
			if (!is_kernel_range(buffer, sizeof(NineTransportInfo)))
				return B_NOT_ALLOWED;
			return fDevice->GetInfo(*static_cast<NineTransportInfo*>(buffer));
		}
		case kIoctl9pTransceive: {
			if (length < sizeof(NineTransceiveArgs))
				return B_BAD_VALUE;
			if (!is_kernel_range(buffer, sizeof(NineTransceiveArgs)))
				return B_NOT_ALLOWED;
			auto *args = static_cast<NineTransceiveArgs*>(buffer);
			if (!is_kernel_range(args->tbuf, args->tlen)
				|| !is_kernel_range(args->rbuf, args->rcap)) {
				return B_NOT_ALLOWED;
			}
			return fDevice->Transceive(args->tbuf, args->tlen, args->rbuf,
				args->rcap, args->rlen);
		}
	}
	return B_DEV_INVALID_IOCTL;
}
