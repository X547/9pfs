#include "NinePfs.h"

#include <fcntl.h>

#include <new>


// 9P Rread header overhead: size[4] type[1] tag[2] count[4].
static const uint32 kReadOverhead = 11;


//#pragma mark - NinePFileVnode

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
	status = Conn().Lopen(openFid, O_RDONLY, qid, iounit);
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
	return B_OK;
}


status_t NinePFileVnode::Close(NinePFileCookie *cookie)
{
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
	uint32 maxChunk = Conn().Msize() - kReadOverhead;
	if (cookie->iounit > 0 && cookie->iounit < maxChunk)
		maxChunk = cookie->iounit;

	uint8 *out = static_cast<uint8*>(buffer);
	size_t want = length;
	size_t total = 0;

	while (total < want) {
		uint32 chunk = (uint32)((want - total < maxChunk) ? (want - total) : maxChunk);
		uint32 got = 0;
		CHECK_RET(Conn().Read(cookie->openFid, (uint64)pos + total, chunk,
			out + total, got));
		if (got == 0)
			break;
		total += got;
	}

	length = total;
	return B_OK;
}
