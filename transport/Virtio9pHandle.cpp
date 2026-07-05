#include "Virtio9pHandle.h"

#include <string.h>


// The transport node is opened and driven only by the 9pfs file system, which
// runs in the kernel, so ioctl buffers are kernel addresses and are used
// directly. It is not intended for userland clients.

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
			if (buffer == nullptr || length < sizeof(NineTransportInfo))
				return B_BAD_VALUE;
			return fDevice->GetInfo(*static_cast<NineTransportInfo*>(buffer));
		}
		case kIoctl9pTransceive: {
			if (buffer == nullptr || length < sizeof(NineTransceiveArgs))
				return B_BAD_VALUE;
			auto *args = static_cast<NineTransceiveArgs*>(buffer);
			return fDevice->Transceive(args->tbuf, args->tlen, args->rbuf,
				args->rcap, args->rlen);
		}
	}
	return B_DEV_INVALID_IOCTL;
}
