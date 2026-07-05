#pragma once

#include <SupportDefs.h>
#include <Drivers.h>


// ioctl contract between the virtio_9p transport driver and the 9pfs file
// system. Both add-ons include this header; it is the only coupling between
// them.

enum {
	kIoctl9pGetInfo    = B_DEVICE_OP_CODES_END + 1,
	kIoctl9pTransceive = B_DEVICE_OP_CODES_END + 2,
};


enum {
	kNineMaxTagLen = 256,
};


// Filled by kIoctl9pGetInfo. msize is the largest 9P message the transport can
// carry in a single kIoctl9pTransceive; the file system negotiates a value no
// larger than this in Tversion.
struct NineTransportInfo {
	uint32 msize;
	char mountTag[kNineMaxTagLen];
};


// Argument for kIoctl9pTransceive. The transport submits [tbuf, tlen) to the
// device as a readable buffer and [rbuf, rcap) as a writable buffer, waits for
// the reply, and reports the reply length in rlen.
struct NineTransceiveArgs {
	const void *tbuf;
	size_t tlen;
	void *rbuf;
	size_t rcap;
	size_t rlen;
};
