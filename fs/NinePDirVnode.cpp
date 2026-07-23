#include "NinePfs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <NodeMonitor.h>

#include <util/AutoLock.h>

#include <new>


// 9P Rreaddir header overhead: size[4] type[1] tag[2] count[4].
static const uint32 kReadDirOverhead = 11;

// Fixed prefix of one Rreaddir entry: qid[13] offset[8] type[1] namelen[2].
static const uint32 kDirEntryPrefix = 24;


static uint16 ReadLE16(const uint8 *p)
{
	return (uint16)p[0] | ((uint16)p[1] << 8);
}


static uint64 ReadLE64(const uint8 *p)
{
	uint64 value = 0;
	for (int i = 0; i < 8; i++)
		value |= (uint64)p[i] << (8 * i);
	return value;
}


//#pragma mark - NinePDirVnode

status_t NinePDirVnode::Lookup(const char *name, ino_t &id)
{
	uint32 newFid;
	if (!Conn().AllocFid(newFid))
		return B_NO_MEMORY;

	NineQid qid;
	status_t status = Conn().Walk(Fid(), newFid, name, qid);
	if (status < B_OK) {
		Conn().FreeFid(newFid);
		return status;
	}

	id = (ino_t)qid.path;

	{
		RecursiveLocker lock(Volume()->Lock());
		NinePVnode *existing = Volume()->FindVnode(id);
		if (existing != nullptr) {
			Conn().Clunk(newFid);
			Conn().FreeFid(newFid);
		} else {
			NinePVnode *vnode = Volume()->GetOrCreateVnode(qid, newFid, name);
			if (vnode == nullptr) {
				Conn().Clunk(newFid);
				Conn().FreeFid(newFid);
				return B_NO_MEMORY;
			}
		}
	}

	return get_vnode(Volume()->Base(), id, NULL);
}


status_t NinePDirVnode::OpenDir(NinePDirCookie *&cookie)
{
	uint32 openFid;
	if (!Conn().AllocFid(openFid))
		return B_NO_MEMORY;

	status_t status = Conn().WalkClone(Fid(), openFid);
	if (status < B_OK) {
		Conn().FreeFid(openFid);
		return status;
	}

	NineQid qid;
	uint32 iounit;
	status = Conn().Lopen(openFid, O_RDONLY, qid, iounit);
	if (status < B_OK) {
		Conn().Clunk(openFid);
		Conn().FreeFid(openFid);
		return status;
	}

	ObjectDeleter<NinePDirCookie> newCookie(new(std::nothrow) NinePDirCookie());
	if (!newCookie.IsSet()) {
		Conn().Clunk(openFid);
		Conn().FreeFid(openFid);
		return B_NO_MEMORY;
	}
	newCookie->chunk.SetTo(new(std::nothrow) uint8[Conn().Msize()]);
	if (!newCookie->chunk.IsSet()) {
		Conn().Clunk(openFid);
		Conn().FreeFid(openFid);
		return B_NO_MEMORY;
	}
	newCookie->openFid = openFid;

	cookie = newCookie.Detach();
	return B_OK;
}


status_t NinePDirVnode::CloseDir(NinePDirCookie *cookie)
{
	if (cookie->openFid != kNineNoFid) {
		Conn().Clunk(cookie->openFid);
		Conn().FreeFid(cookie->openFid);
		cookie->openFid = kNineNoFid;
	}
	return B_OK;
}


status_t NinePDirVnode::FreeDirCookie(NinePDirCookie *cookie)
{
	delete cookie;
	return B_OK;
}


status_t NinePDirVnode::ReadDir(NinePDirCookie *cookie, struct dirent *buffer,
	size_t bufferSize, uint32 &num)
{
	uint32 maxNum = num;
	num = 0;
	uint32 maxData = Conn().Msize() - kReadDirOverhead;

	while (num < maxNum) {
		if (cookie->chunkPos >= cookie->chunkLength) {
			if (cookie->eof)
				break;
			uint32 got = 0;
			CHECK_RET(Conn().ReadDir(cookie->openFid, cookie->nextOffset,
				maxData, &cookie->chunk[0], got));
			cookie->chunkLength = got;
			cookie->chunkPos = 0;
			if (got == 0) {
				cookie->eof = true;
				break;
			}
		}

		const uint8 *p = &cookie->chunk[cookie->chunkPos];
		uint32 remaining = cookie->chunkLength - cookie->chunkPos;
		if (remaining < kDirEntryPrefix) {
			cookie->eof = true;
			break;
		}

		uint64 qidPath = ReadLE64(p + 5);
		uint64 entryOffset = ReadLE64(p + 13);
		uint16 nameLen = ReadLE16(p + 22);
		const uint8 *namePtr = p + kDirEntryPrefix;
		if (kDirEntryPrefix + (uint32)nameLen > remaining) {
			cookie->eof = true;
			break;
		}

		size_t direntSize = offsetof(struct dirent, d_name) + nameLen + 1;
		if (bufferSize < direntSize) {
			if (num == 0)
				return B_BUFFER_OVERFLOW;
			break;
		}

		*buffer = {
			.d_dev = Volume()->Id(),
			.d_ino = (ino_t)qidPath,
			.d_reclen = (uint16)direntSize,
		};
		memcpy(buffer->d_name, namePtr, nameLen);
		buffer->d_name[nameLen] = '\0';

		bufferSize -= direntSize;
		buffer = (struct dirent*)((uint8*)buffer + direntSize);
		num++;

		cookie->chunkPos += kDirEntryPrefix + nameLen;
		cookie->nextOffset = entryOffset;
	}

	return B_OK;
}


status_t NinePDirVnode::RewindDir(NinePDirCookie *cookie)
{
	cookie->nextOffset = 0;
	cookie->chunkLength = 0;
	cookie->chunkPos = 0;
	cookie->eof = false;
	return B_OK;
}


//#pragma mark - mutations

// New nodes are created on the host with group 0; QEMU maps ownership per its
// security model, and the caller-visible owner is the host's.
static const uint32 kNineCreateGid = 0;


status_t NinePDirVnode::ResolveChild(const char *name, ino_t &id)
{
	uint32 probeFid;
	if (!Conn().AllocFid(probeFid))
		return B_NO_MEMORY;

	NineQid qid;
	status_t status = Conn().Walk(Fid(), probeFid, name, qid);
	if (status < B_OK) {
		Conn().FreeFid(probeFid);
		return status;
	}

	id = (ino_t)qid.path;
	Conn().Clunk(probeFid);
	Conn().FreeFid(probeFid);
	return B_OK;
}


status_t NinePDirVnode::Create(const char *name, int openMode, int perms,
	NinePFileCookie *&cookie, ino_t &newVnodeID)
{
	uint32 probeFid;
	if (!Conn().AllocFid(probeFid))
		return B_NO_MEMORY;

	NineQid qid;
	status_t walkStatus = Conn().Walk(Fid(), probeFid, name, qid);

	if (walkStatus == B_OK) {
		// The entry already exists.
		if ((openMode & O_EXCL) != 0) {
			Conn().Clunk(probeFid);
			Conn().FreeFid(probeFid);
			return B_FILE_EXISTS;
		}

		ino_t id = (ino_t)qid.path;
		NinePVnode *vnode;
		{
			RecursiveLocker lock(Volume()->Lock());
			NinePVnode *existing = Volume()->FindVnode(id);
			if (existing != nullptr) {
				Conn().Clunk(probeFid);
				Conn().FreeFid(probeFid);
				vnode = existing;
			} else {
				vnode = Volume()->GetOrCreateVnode(qid, probeFid, name);
				if (vnode == nullptr) {
					Conn().Clunk(probeFid);
					Conn().FreeFid(probeFid);
					return B_NO_MEMORY;
				}
			}
		}
		CHECK_RET(get_vnode(Volume()->Base(), id, NULL));
		status_t status = vnode->Open(openMode, cookie);
		if (status < B_OK) {
			put_vnode(Volume()->Base(), id);
			return status;
		}
		newVnodeID = id;
		return B_OK;
	}

	if (walkStatus != B_ENTRY_NOT_FOUND) {
		Conn().FreeFid(probeFid);
		return walkStatus;
	}
	Conn().FreeFid(probeFid);

	// The entry does not exist: create and open it. Tlcreate leaves createFid
	// open on the new file; a second walk yields the unopened vnode fid.
	uint32 createFid;
	if (!Conn().AllocFid(createFid))
		return B_NO_MEMORY;
	status_t status = Conn().WalkClone(Fid(), createFid);
	if (status < B_OK) {
		Conn().FreeFid(createFid);
		return status;
	}

	NineQid newQid;
	uint32 iounit;
	status = Conn().Lcreate(createFid, name, NineHaikuToLinuxOpenFlags(openMode),
		perms & 07777, kNineCreateGid, newQid, iounit);
	if (status < B_OK) {
		Conn().Clunk(createFid);
		Conn().FreeFid(createFid);
		return status;
	}

	uint32 vnodeFid;
	if (!Conn().AllocFid(vnodeFid)) {
		Conn().Clunk(createFid);
		Conn().FreeFid(createFid);
		return B_NO_MEMORY;
	}
	NineQid vnodeQid;
	status = Conn().Walk(Fid(), vnodeFid, name, vnodeQid);
	if (status < B_OK) {
		Conn().FreeFid(vnodeFid);
		Conn().Clunk(createFid);
		Conn().FreeFid(createFid);
		return status;
	}

	newVnodeID = (ino_t)newQid.path;
	{
		RecursiveLocker lock(Volume()->Lock());
		if (Volume()->FindVnode(newVnodeID) != nullptr) {
			Conn().Clunk(vnodeFid);
			Conn().FreeFid(vnodeFid);
		} else if (Volume()->GetOrCreateVnode(vnodeQid, vnodeFid, name) == nullptr) {
			Conn().Clunk(vnodeFid);
			Conn().FreeFid(vnodeFid);
			Conn().Clunk(createFid);
			Conn().FreeFid(createFid);
			return B_NO_MEMORY;
		}
	}

	status = get_vnode(Volume()->Base(), newVnodeID, NULL);
	if (status < B_OK) {
		Conn().Clunk(createFid);
		Conn().FreeFid(createFid);
		return status;
	}

	cookie = new(std::nothrow) NinePFileCookie();
	if (cookie == nullptr) {
		Conn().Clunk(createFid);
		Conn().FreeFid(createFid);
		put_vnode(Volume()->Base(), newVnodeID);
		return B_NO_MEMORY;
	}
	cookie->openFid = createFid;
	cookie->iounit = iounit;

	notify_entry_created(Volume()->Id(), Id(), name, newVnodeID);
	return B_OK;
}


status_t NinePDirVnode::CreateDir(const char *name, int perms)
{
	NineQid qid;
	CHECK_RET(Conn().Mkdir(Fid(), name, perms & 07777, kNineCreateGid, qid));
	notify_entry_created(Volume()->Id(), Id(), name, (ino_t)qid.path);
	return B_OK;
}


status_t NinePDirVnode::CreateSymlink(const char *name, const char *path, int mode)
{
	NineQid qid;
	CHECK_RET(Conn().Symlink(Fid(), name, path, kNineCreateGid, qid));
	notify_entry_created(Volume()->Id(), Id(), name, (ino_t)qid.path);
	return B_OK;
}


status_t NinePDirVnode::Unlink(const char *name)
{
	ino_t id;
	CHECK_RET(ResolveChild(name, id));

	CHECK_RET(Conn().UnlinkAt(Fid(), name, 0));

	// Mark a resident vnode removed so VFS tears it down (via remove_vnode) when
	// its last reference drops. get_vnode reinstantiates from fIds; a node that
	// was never resident simply isn't there and needs no teardown.
	if (get_vnode(Volume()->Base(), id, NULL) == B_OK) {
		remove_vnode(Volume()->Base(), id);
		put_vnode(Volume()->Base(), id);
	}
	notify_entry_removed(Volume()->Id(), Id(), name, id);
	return B_OK;
}


status_t NinePDirVnode::RemoveDir(const char *name)
{
	ino_t id;
	CHECK_RET(ResolveChild(name, id));

	CHECK_RET(Conn().UnlinkAt(Fid(), name, kNineAtRemoveDir));

	if (get_vnode(Volume()->Base(), id, NULL) == B_OK) {
		remove_vnode(Volume()->Base(), id);
		put_vnode(Volume()->Base(), id);
	}
	notify_entry_removed(Volume()->Id(), Id(), name, id);
	return B_OK;
}


status_t NinePDirVnode::Rename(const char *fromName, NinePVnode *toDir,
	const char *toName)
{
	ino_t id;
	CHECK_RET(ResolveChild(fromName, id));

	CHECK_RET(Conn().RenameAt(Fid(), fromName, toDir->Fid(), toName));

	{
		RecursiveLocker lock(Volume()->Lock());
		NinePVnode *vnode = Volume()->FindVnode(id);
		if (vnode != nullptr)
			vnode->SetName(toName);
	}
	notify_entry_moved(Volume()->Id(), Id(), fromName, toDir->Id(), toName, id);
	return B_OK;
}
