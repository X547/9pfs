#include "NinePfs.h"

#include <fs_info.h>
#include <sys/stat.h>

#include <util/AutoLock.h>

#include <new>


//#pragma mark - vnode registry

NinePVnode *NinePVolume::GetOrCreateVnode(const NineQid &qid, uint32 fid,
	const char *name)
{
	NinePVnode *existing = fIds.Find((ino_t)qid.path);
	if (existing != nullptr)
		return existing;

	NinePVnode *vnode = nullptr;
	switch (NineTypeToStatType(qid.type)) {
		case S_IFDIR:
			vnode = new(std::nothrow) NinePDirVnode(qid, fid);
			break;
		case S_IFLNK:
			vnode = new(std::nothrow) NinePSymlinkVnode(qid, fid);
			break;
		default:
			vnode = new(std::nothrow) NinePFileVnode(qid, fid);
			break;
	}
	if (vnode == nullptr)
		return nullptr;

	vnode->fVolume = this;
	if (name != nullptr)
		vnode->SetName(name);
	fIds.Insert(vnode);
	return vnode;
}


NinePVnode *NinePVolume::FindVnode(ino_t id)
{
	return fIds.Find(id);
}


void NinePVolume::RemoveVnode(NinePVnode *vnode)
{
	RecursiveLocker lock(Lock());
	fIds.Remove(vnode);
	if (vnode->Fid() != kNineNoFid)
		fConn.Clunk(vnode->Fid());
	delete vnode;
}


//#pragma mark - NinePVolume

status_t NinePVolume::Mount(NinePVolume *&volume, fs_volume *base,
	const char *device, uint32 flags, const char *args, ino_t &rootVnodeID)
{
	ObjectDeleter<NinePVolume> vol(new(std::nothrow) NinePVolume());
	if (!vol.IsSet())
		return B_NO_MEMORY;

	RecursiveLocker lock(vol->Lock());
	vol->fBase = base;
	// Publish private_volume before get_vnode(), which reenters the GetVnode
	// hook and dereferences volume->private_volume.
	volume = vol.Get();

	// The kernel refuses a device path that has no DDM partition, so 9pfs is
	// mounted with device == NULL (like nfs) and the transport node is taken
	// from the mount parameters, falling back to the first published node.
	const char *transportPath = "/dev/misc/virtio_9p/0";
	if (device != NULL && device[0] != '\0')
		transportPath = device;
	else if (args != NULL && args[0] != '\0')
		transportPath = args;

	uint32 transportMsize = 0;
	char mountTag[kNineMaxTagLen] = {};
	CHECK_RET(vol->fConn.Init(transportPath, transportMsize, mountTag, sizeof(mountTag)));
	CHECK_RET(vol->fConn.Version(transportMsize));

	uint32 rootFid;
	if (!vol->fConn.AllocFid(rootFid))
		return B_NO_MEMORY;

	NineQid rootQid;
	CHECK_RET(vol->fConn.Attach(rootFid, "root", "", rootQid));

	vol->fRootVnode = vol->GetOrCreateVnode(rootQid, rootFid, "");
	if (vol->fRootVnode == nullptr)
		return B_NO_MEMORY;
	rootVnodeID = vol->fRootVnode->Id();

	strlcpy(vol->fVolumeName, mountTag[0] != '\0' ? mountTag : "9pfs",
		sizeof(vol->fVolumeName));

	CHECK_RET(get_vnode(vol->Base(), rootVnodeID, NULL));

	vol.Detach();
	return B_OK;
}


status_t NinePVolume::Unmount()
{
	// The VFS frees every vnode but the root, which keeps the reference
	// Mount() took.
	put_vnode(Base(), fRootVnode->Id());

	{
		RecursiveLocker lock(Lock());
		for (;;) {
			NinePVnode *vnode = fIds.LeftMost();
			if (vnode == nullptr)
				break;
			fIds.Remove(vnode);
			if (vnode->Fid() != kNineNoFid)
				fConn.Clunk(vnode->Fid());
			delete vnode;
		}
	}
	delete this;
	return B_OK;
}


status_t NinePVolume::ReadFsInfo(struct fs_info &info)
{
	RecursiveLocker lock(Lock());
	info = {
		.dev = Id(),
		.root = fRootVnode->Id(),
		.flags = B_FS_IS_SHARED | B_FS_IS_PERSISTENT,
		.block_size = 512,
		.io_size = (off_t)fConn.Msize(),
		// Without an answer from the server, report a huge volume with all of
		// it free, so that copying to it is not refused.
		.total_blocks = INT64_MAX / 512,
		.free_blocks = INT64_MAX / 512,
		.total_nodes = INT64_MAX,
		.free_nodes = INT64_MAX,
	};

	NineStatfs st;
	if (fConn.Statfs(fRootVnode->Fid(), st) == B_OK && st.bsize != 0) {
		info.block_size = st.bsize;
		info.total_blocks = (off_t)st.blocks;
		// what an unprivileged user may still use
		info.free_blocks = (off_t)st.bavail;
		// a server with no limit on the number of files reports none
		if (st.files != 0) {
			info.total_nodes = (off_t)st.files;
			info.free_nodes = (off_t)st.ffree;
		}
	}

	strlcpy(info.volume_name, fVolumeName, sizeof(info.volume_name));
	return B_OK;
}


status_t NinePVolume::GetVnode(ino_t id, NinePVnode *&vnode, int &type,
	uint32 &flags, bool reenter)
{
	RecursiveLocker lock(Lock());

	NinePVnode *found = fIds.Find(id);
	if (found == nullptr)
		return B_ENTRY_NOT_FOUND;

	type = NineTypeToStatType(found->Qid().type);
	flags = 0;
	vnode = found;
	return B_OK;
}
