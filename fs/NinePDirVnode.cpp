#include "NinePfs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>

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
