#pragma once

#include <SupportDefs.h>


// VirtIO 9P transport device definitions (QEMU virtio-9p-pci / virtio-9p-mmio).

enum {
	// If negotiated, the device config space carries a mount tag.
	kVirtio9pFeatureMountTag = 1 << 0,
};


// Layout of the VirtIO 9P device configuration space: a little-endian uint16
// tag length at offset 0, followed by that many tag bytes (not null-terminated).
enum {
	kVirtio9pConfigTagLenOffset = 0,
	kVirtio9pConfigTagOffset    = 2,
};
