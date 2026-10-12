#include "NinePfs.h"

#include <fcntl.h>
#include <sys/uio.h>

#include <fs_cache.h>

#include <util/AutoLock.h>

#include <new>


// 9P Rread header overhead: size[4] type[1] tag[2] count[4].
static const uint32 kReadOverhead = 11;

// 9P Twrite header overhead: size[4] type[1] tag[2] fid[4] offset[8] count[4].
static const uint32 kWriteOverhead = 23;


//#pragma mark - NinePFileVnode

NinePFileVnode::~NinePFileVnode()
{
	// Delete the cache first so any dirty pages flush through WritePages while
	// the cache fid is still open.
	if (fFileCache != nullptr)
		file_cache_delete(fFileCache);
	if (fCacheFid != kNineNoFid) {
		Conn().Clunk(fCacheFid);
		Conn().FreeFid(fCacheFid);
	}
}


status_t NinePFileVnode::EnsureCache()
{
	RecursiveLocker lock(Volume()->Lock());
	if (fFileCache != nullptr)
		return B_OK;

	NineStat stat;
	CHECK_RET(Conn().GetAttr(Fid(), kNineGetattrSize, stat));
	fCacheSize = (off_t)stat.size;

	// Open a dedicated fid for page I/O. Page cache reads/writes can happen
	// without an open cookie (page faults, writeback), so the vnode needs its
	// own opened fid. Prefer read/write, fall back to read-only.
	uint32 fid;
	if (!Conn().AllocFid(fid))
		return B_NO_MEMORY;
	status_t status = Conn().WalkClone(Fid(), fid);
	if (status < B_OK) {
		Conn().FreeFid(fid);
		return status;
	}

	NineQid qid;
	uint32 iounit;
	status = Conn().Lopen(fid, 2 /* O_RDWR */, qid, iounit);
	if (status < B_OK) {
		Conn().Clunk(fid);
		Conn().FreeFid(fid);
		if (!Conn().AllocFid(fid))
			return B_NO_MEMORY;
		status = Conn().WalkClone(Fid(), fid);
		if (status < B_OK) {
			Conn().FreeFid(fid);
			return status;
		}
		status = Conn().Lopen(fid, 0 /* O_RDONLY */, qid, iounit);
		if (status < B_OK) {
			Conn().Clunk(fid);
			Conn().FreeFid(fid);
			return status;
		}
	}
	fCacheFid = fid;

	fFileCache = file_cache_create(Volume()->Id(), Id(), fCacheSize);
	if (fFileCache == nullptr) {
		Conn().Clunk(fCacheFid);
		Conn().FreeFid(fCacheFid);
		fCacheFid = kNineNoFid;
		return B_NO_MEMORY;
	}
	return B_OK;
}


status_t NinePFileVnode::Open(int openMode, NinePFileCookie *&cookie)
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
	status = Conn().Lopen(openFid, NineHaikuToLinuxOpenFlags(openMode), qid, iounit);
	if (status < B_OK) {
		Conn().Clunk(openFid);
		Conn().FreeFid(openFid);
		return status;
	}

	cookie = new(std::nothrow) NinePFileCookie();
	if (cookie == nullptr) {
		Conn().Clunk(openFid);
		Conn().FreeFid(openFid);
		return B_NO_MEMORY;
	}
	cookie->openFid = openFid;
	cookie->iounit = iounit;
	cookie->openMode = openMode;

	// The server has emptied the file; the cache must not keep, or write back,
	// what it held of it.
	if ((openMode & O_TRUNC) != 0)
		OnSizeChanged(0);

	// A failure to set up the cache here is not fatal to opening; cached I/O
	// falls back lazily on the next access.
	EnsureCache();
	return B_OK;
}


status_t NinePFileVnode::Close(NinePFileCookie *cookie)
{
	if (fFileCache != nullptr)
		file_cache_sync(fFileCache);
	if (cookie->openFid != kNineNoFid) {
		Conn().Clunk(cookie->openFid);
		Conn().FreeFid(cookie->openFid);
		cookie->openFid = kNineNoFid;
	}
	return B_OK;
}


status_t NinePFileVnode::FreeCookie(NinePFileCookie *cookie)
{
	delete cookie;
	return B_OK;
}


status_t NinePFileVnode::Read(NinePFileCookie *cookie, off_t pos, void *buffer,
	size_t &length)
{
	CHECK_RET(EnsureCache());
	return file_cache_read(fFileCache, pos, buffer, &length);
}


status_t NinePFileVnode::Write(NinePFileCookie *cookie, off_t pos, const void *buffer,
	size_t &length)
{
	CHECK_RET(EnsureCache());

	{
		RecursiveLocker lock(Volume()->Lock());
		// The VFS leaves O_APPEND to the file system.
		if (cookie != nullptr && (cookie->openMode & O_APPEND) != 0)
			pos = fCacheSize;
		if (pos + (off_t)length > fCacheSize) {
			fCacheSize = pos + (off_t)length;
			file_cache_set_size(fFileCache, fCacheSize);
		}
	}

	return file_cache_write(fFileCache, pos, buffer, &length);
}


void NinePFileVnode::OnSizeChanged(off_t newSize)
{
	RecursiveLocker lock(Volume()->Lock());
	fCacheSize = newSize;
	if (fFileCache != nullptr)
		file_cache_set_size(fFileCache, newSize);
}


status_t NinePFileVnode::ReadPages(off_t pos, const struct iovec *vecs, size_t count,
	size_t &numBytes)
{
	uint32 maxChunk = Conn().Msize() - kReadOverhead;

	size_t total = 0;
	bool eof = false;
	for (size_t i = 0; i < count && !eof; i++) {
		uint8 *buffer = static_cast<uint8*>(vecs[i].iov_base);
		size_t left = vecs[i].iov_len;
		while (left > 0) {
			uint32 chunk = (uint32)(left < maxChunk ? left : maxChunk);
			uint32 got = 0;
			CHECK_RET(Conn().Read(fCacheFid, (uint64)pos, chunk, buffer, got));
			if (got == 0) {
				eof = true;
				break;
			}
			total += got;
			pos += got;
			buffer += got;
			left -= got;
		}
	}

	numBytes = total;
	return B_OK;
}


status_t NinePFileVnode::WritePages(off_t pos, const struct iovec *vecs, size_t count,
	size_t &numBytes)
{
	uint32 maxChunk = Conn().Msize() - kWriteOverhead;

	off_t size;
	{
		RecursiveLocker lock(Volume()->Lock());
		size = fCacheSize;
	}

	// The cache flushes whole pages; write only the portion within the logical
	// file size so the last partial page isn't zero-padded on the server. pos
	// advances by the full vec length since vecs map contiguous file offsets.
	size_t requested = 0;
	size_t written = 0;
	for (size_t i = 0; i < count; i++) {
		requested += vecs[i].iov_len;

		off_t segStart = pos;
		pos += (off_t)vecs[i].iov_len;

		size_t writeLen = 0;
		if (segStart < size) {
			off_t segEnd = segStart + (off_t)vecs[i].iov_len;
			writeLen = (size_t)((segEnd > size ? size : segEnd) - segStart);
		}

		const uint8 *buffer = static_cast<const uint8*>(vecs[i].iov_base);
		off_t wpos = segStart;
		size_t left = writeLen;
		while (left > 0) {
			uint32 chunk = (uint32)(left < maxChunk ? left : maxChunk);
			uint32 wrote = 0;
			CHECK_RET(Conn().Write(fCacheFid, (uint64)wpos, chunk, buffer, wrote));
			if (wrote == 0)
				break;
			written += wrote;
			wpos += wrote;
			buffer += wrote;
			left -= wrote;
		}
	}

	// Report all requested bytes as handled; pages beyond EOF legitimately map
	// to nothing on the server and must not be retried as dirty.
	numBytes = requested;
	return B_OK;
}
