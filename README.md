# 9pfs for Haiku

A 9P (Plan 9) file system client for [Haiku](https://www.haiku-os.org/), speaking
the **9P2000.L** dialect over **virtio-9p**. It lets a Haiku guest running under
QEMU (or another virtio-9p capable hypervisor) mount a directory exported by the
host.

The project consists of two kernel add-ons:

| Directory     | Add-on      | Kind                       | Role |
| ------------- | ----------- | -------------------------- | ---- |
| `transport/`  | `virtio_9p` | device manager driver      | Talks to the virtio-9p PCI device, publishes `/dev/misc/virtio_9p/N` and exposes an ioctl based message transport. |
| `fs/`         | `9pfs`      | file system module         | Implements the 9P2000.L client and the Haiku VFS hooks on top of that transport. |

The only coupling between the two is [transport/NineTransport.h](transport/NineTransport.h),
which defines the `kIoctl9pGetInfo` / `kIoctl9pTransceive` ioctl contract. A
different transport (TCP, virtio on another host, …) can be plugged in by
publishing a device node that implements the same two ioctls.

## Requirements

- Haiku **x86_64** (the makefiles reference the x86/x86_64 private kernel headers).
- Haiku development files (private headers under `/boot/system/develop/headers/private`)
  and the standard build tools — both come with a default Haiku installation.
- A host exporting a directory over virtio-9p, e.g. QEMU.

## Building

Both add-ons are built with the Haiku generic makefile engine. Build them on the
Haiku machine (or VM) itself:

```sh
cd transport && make
cd ../fs && make
```

Each `make` writes its binary into an `objects.x86_64-*-release` subdirectory:

- `transport/objects.x86_64-*-release/virtio_9p`
- `fs/objects.x86_64-*-release/9pfs`

Use `make clean` to remove build products.

## Installing

Copy both binaries into the non-packaged kernel add-on directories. `make install`
from the makefile engine is meant for legacy drivers and is *not* used here.

```sh
mkdir -p ~/config/non-packaged/add-ons/kernel/drivers/misc
mkdir -p ~/config/non-packaged/add-ons/kernel/file_systems

cp transport/objects.x86_64-*-release/virtio_9p \
	~/config/non-packaged/add-ons/kernel/drivers/misc/
cp fs/objects.x86_64-*-release/9pfs \
	~/config/non-packaged/add-ons/kernel/file_systems/
```

`virtio_9p` is a device manager driver, so it is picked up when the device
manager probes the virtio bus — reboot after installing or updating it. The
`9pfs` module is loaded on demand by the VFS; it is re-read on the next mount
once every 9pfs volume has been unmounted, so replacing it does not require a
reboot.

After a reboot the transport node should be visible:

```sh
~> ls /dev/misc/virtio_9p
0
```

If it is missing, check `/var/log/syslog` for `virtio_9p:` messages and confirm
that the guest actually has a virtio-9p device (`listdev`).

## Host setup (QEMU)

Export a host directory as a virtio-9p device:

```sh
qemu-system-x86_64 \
	... \
	-fsdev local,id=fsdev0,path=/path/to/share,security_model=none \
	-device virtio-9p-pci,fsdev=fsdev0,mount_tag=hostshare
```

The `mount_tag` is read from the device configuration and becomes the Haiku
volume name; when the device advertises no tag, the volume is named `9pfs`.

QEMU's virtio-9p implementation is what this client is developed against.
`security_model=none` (or `mapped`) keeps QEMU from requiring root privileges
for ownership changes.

## Mounting

The kernel rejects a mount device path that is not a DDM partition, so — like
`nfs` — 9pfs is mounted **without** a device argument and takes the transport
node from the mount parameters instead:

```sh
mkdir /9p
mount -t 9pfs -o /dev/misc/virtio_9p/0 /9p
```

If no parameter is given, `/dev/misc/virtio_9p/0` is used as the default, so for
a single exported share this is enough:

```sh
mount -t 9pfs /9p
```

Passing the transport node as the *device* argument (`mount -t 9pfs /dev/misc/virtio_9p/0 /9p`)
fails with `No such file or directory` — that is the DDM restriction above, not a
missing device.

Unmount as usual:

```sh
unmount /9p
```

## What works

- Directory listing, lookup, `stat`.
- Reading and writing files, through the file cache (`read_pages` / `write_pages`).
- Creating and removing files and directories, rename, unlink.
- Symlinks: reading and creation.
- Changing attributes via `write_stat` (mode, size, times).

## Limitations

- 9P2000.L only. A server that negotiates a different version is rejected during
  `Tversion`.
- Hard links are not implemented.
- No `statfs`: total/free space is reported as a fixed placeholder, free space as
  zero.
- No BFS extras — attributes, indices and queries are not supported.
- `msize` is fixed at 8 KiB by the transport, and the transport serializes one
  outstanding request at a time with a 10 second reply timeout.
- Mount flags (including read-only) are ignored.
- Server errors arrive as Linux errno values and are mapped to Haiku status codes
  for the common cases only; anything unmapped surfaces as a generic error.

## Debugging

Both add-ons carry a `TRACE` macro that is compiled out by default; enable it by
flipping the `#if 0` at the top of the relevant source file and rebuilding.
Kernel output goes to `/var/log/syslog`:

```sh
tail -f /var/log/syslog
```

## License

MIT — see [LICENSE](LICENSE).
