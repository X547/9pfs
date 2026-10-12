#include "NineConnection.h"
#include "NineTransport.h"

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>

#include <util/AutoLock.h>

#include <new>


#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


// 9P2000.L Rlerror carries a Linux errno, whose numeric values differ from
// Haiku's. Translate the ones the server realistically returns.
static status_t NineErrnoToStatus(uint32 linuxErrno)
{
	switch (linuxErrno) {
		case 1:  return EPERM;
		case 2:  return B_ENTRY_NOT_FOUND;
		case 5:  return B_IO_ERROR;
		case 9:  return B_FILE_ERROR;
		case 11: return B_WOULD_BLOCK;
		case 12: return B_NO_MEMORY;
		case 13: return B_PERMISSION_DENIED;
		case 16: return B_BUSY;
		case 17: return B_FILE_EXISTS;
		case 18: return B_CROSS_DEVICE_LINK;
		case 20: return B_NOT_A_DIRECTORY;
		case 21: return B_IS_A_DIRECTORY;
		case 22: return B_BAD_VALUE;
		case 24: return EMFILE;
		case 28: return B_DEVICE_FULL;
		case 30: return B_READ_ONLY_DEVICE;
		case 36: return B_NAME_TOO_LONG;
		case 38: return B_NOT_SUPPORTED;
		case 39: return B_DIRECTORY_NOT_EMPTY;
		case 40: return ELOOP;
	}
	return B_ERROR;
}


NineConnection::NineConnection()
{
	// Without memory for it the pool stays empty, and mounting fails at the
	// first fid.
	fFidPool.Register(0, 0xfffffffe);
}


NineConnection::~NineConnection()
{
	if (fFd >= 0)
		close(fFd);
	mutex_destroy(&fLock);
}


status_t NineConnection::Init(const char *devicePath, uint32 &transportMsize,
	char *mountTag, size_t mountTagSize)
{
	fFd = open(devicePath, O_RDWR);
	if (fFd < 0)
		return errno;

	NineTransportInfo info = {};
	if (ioctl(fFd, kIoctl9pGetInfo, &info, sizeof(info)) < 0)
		return errno;

	fMsize = info.msize;
	transportMsize = info.msize;
	strlcpy(mountTag, info.mountTag, mountTagSize);

	fSendBuf.SetTo(new(std::nothrow) uint8[fMsize]);
	fRecvBuf.SetTo(new(std::nothrow) uint8[fMsize]);
	if (!fSendBuf.IsSet() || !fRecvBuf.IsSet())
		return B_NO_MEMORY;

	return B_OK;
}


// The pool is shared by every thread using the volume.
bool NineConnection::AllocFid(uint32 &fid)
{
	MutexLocker locker(&fLock);
	uint64 value;
	if (!fFidPool.Alloc(value, 1))
		return false;
	fid = (uint32)value;
	return true;
}


void NineConnection::FreeFid(uint32 fid)
{
	MutexLocker locker(&fLock);
	fFidPool.Free(fid);
}


// Each request has a tag of its own, so that a reply to another one is never
// taken for its own.
uint16 NineConnection::NewTag()
{
	uint16 tag = fNextTag++;
	if (fNextTag == kNineNoTag)
		fNextTag = 0;
	return tag;
}


status_t NineConnection::Transceive(size_t tlen, size_t &rlen)
{
	NineTransceiveArgs args = {
		.tbuf = &fSendBuf[0],
		.tlen = tlen,
		.rbuf = &fRecvBuf[0],
		.rcap = fMsize,
		.rlen = 0,
	};
	if (ioctl(fFd, kIoctl9pTransceive, &args, sizeof(args)) < 0)
		return errno;
	if (args.rlen < kNineHeaderSize)
		return B_IO_ERROR;
	if (fRecvBuf[5] != fSendBuf[5] || fRecvBuf[6] != fSendBuf[6])
		return B_IO_ERROR;
	rlen = args.rlen;
	return B_OK;
}


status_t NineConnection::CheckReply(NineReader &reader, NineType expected)
{
	if (!reader.Ok())
		return B_IO_ERROR;
	NineType type = reader.Type();
	if (type == expected)
		return B_OK;
	if (type == NineType::Rlerror) {
		uint32 ecode = reader.GetU32();
		return NineErrnoToStatus(ecode);
	}
	return B_IO_ERROR;
}


//#pragma mark - typed requests

status_t NineConnection::Version(uint32 wantMsize)
{
	MutexLocker locker(&fLock);

	// Tversion uses NOTAG; the request negotiates msize and protocol version.
	NineWriter w(&fSendBuf[0], fMsize, NineType::Tversion, kNineNoTag);
	w.PutU32(wantMsize);
	w.PutString("9P2000.L");
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rversion));

	uint32 serverMsize = r.GetU32();
	const char *version;
	uint16 versionLen;
	r.GetString(version, versionLen);
	if (!r.Ok())
		return B_IO_ERROR;

	if (strncmp(version, "9P2000.L", versionLen) != 0 || versionLen != 8)
		return B_NOT_SUPPORTED;

	if (serverMsize < fMsize)
		fMsize = serverMsize;

	return B_OK;
}


status_t NineConnection::Attach(uint32 fid, const char *uname, const char *aname,
	NineQid &outQid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tattach, NewTag());
	w.PutU32(fid);
	w.PutU32(kNineNoFid);
	w.PutString(uname);
	w.PutString(aname);
	w.PutU32(kNineNoNUname);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rattach));

	r.GetQid(outQid);
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::WalkClone(uint32 fid, uint32 newFid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Twalk, NewTag());
	w.PutU32(fid);
	w.PutU32(newFid);
	w.PutU16(0);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	return CheckReply(r, NineType::Rwalk);
}


status_t NineConnection::Walk(uint32 fid, uint32 newFid, const char *name,
	NineQid &outQid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Twalk, NewTag());
	w.PutU32(fid);
	w.PutU32(newFid);
	w.PutU16(1);
	w.PutString(name);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rwalk));

	uint16 nwqid = r.GetU16();
	if (nwqid != 1)
		return B_ENTRY_NOT_FOUND;
	r.GetQid(outQid);
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::GetAttr(uint32 fid, uint64 mask, NineStat &outStat)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tgetattr, NewTag());
	w.PutU32(fid);
	w.PutU64(mask);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rgetattr));

	r.GetU64(); // valid mask
	r.GetQid(outStat.qid);
	outStat.mode = r.GetU32();
	outStat.uid = r.GetU32();
	outStat.gid = r.GetU32();
	outStat.nlink = r.GetU64();
	r.GetU64(); // rdev
	outStat.size = r.GetU64();
	r.GetU64(); // blksize
	outStat.blocks = r.GetU64();
	outStat.atimeSec = (int64)r.GetU64();
	outStat.atimeNsec = (int64)r.GetU64();
	outStat.mtimeSec = (int64)r.GetU64();
	outStat.mtimeNsec = (int64)r.GetU64();
	outStat.ctimeSec = (int64)r.GetU64();
	outStat.ctimeNsec = (int64)r.GetU64();
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::Statfs(uint32 fid, NineStatfs &outStatfs)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tstatfs, NewTag());
	w.PutU32(fid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rstatfs));

	r.GetU32(); // type
	outStatfs.bsize = r.GetU32();
	outStatfs.blocks = r.GetU64();
	outStatfs.bfree = r.GetU64();
	outStatfs.bavail = r.GetU64();
	outStatfs.files = r.GetU64();
	outStatfs.ffree = r.GetU64();
	r.GetU64(); // fsid
	r.GetU32(); // namelen
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::Lopen(uint32 fid, uint32 flags, NineQid &outQid,
	uint32 &outIounit)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tlopen, NewTag());
	w.PutU32(fid);
	w.PutU32(flags);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rlopen));

	r.GetQid(outQid);
	outIounit = r.GetU32();
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::Read(uint32 fid, uint64 offset, uint32 count,
	void *buffer, uint32 &outCount)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tread, NewTag());
	w.PutU32(fid);
	w.PutU64(offset);
	w.PutU32(count);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rread));

	// more than was asked for would overrun the caller's buffer
	uint32 replyCount = r.GetU32();
	const uint8 *data = r.GetData(replyCount);
	if (!r.Ok() || replyCount > count)
		return B_IO_ERROR;
	memcpy(buffer, data, replyCount);
	outCount = replyCount;
	return B_OK;
}


status_t NineConnection::ReadDir(uint32 fid, uint64 offset, uint32 count,
	void *buffer, uint32 &outLength)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Treaddir, NewTag());
	w.PutU32(fid);
	w.PutU64(offset);
	w.PutU32(count);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rreaddir));

	uint32 replyCount = r.GetU32();
	const uint8 *data = r.GetData(replyCount);
	if (!r.Ok() || replyCount > count)
		return B_IO_ERROR;
	memcpy(buffer, data, replyCount);
	outLength = replyCount;
	return B_OK;
}


status_t NineConnection::ReadLink(uint32 fid, char *buffer, size_t bufferSize)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Treadlink, NewTag());
	w.PutU32(fid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rreadlink));

	const char *target;
	uint16 targetLen;
	if (!r.GetString(target, targetLen))
		return B_IO_ERROR;
	size_t copyLen = targetLen < bufferSize - 1 ? targetLen : bufferSize - 1;
	memcpy(buffer, target, copyLen);
	buffer[copyLen] = '\0';
	return B_OK;
}


status_t NineConnection::Clunk(uint32 fid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tclunk, NewTag());
	w.PutU32(fid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	return CheckReply(r, NineType::Rclunk);
}


//#pragma mark - mutations

status_t NineConnection::Lcreate(uint32 fid, const char *name, uint32 flags,
	uint32 mode, uint32 gid, NineQid &outQid, uint32 &outIounit)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tlcreate, NewTag());
	w.PutU32(fid);
	w.PutString(name);
	w.PutU32(flags);
	w.PutU32(mode);
	w.PutU32(gid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rlcreate));

	r.GetQid(outQid);
	outIounit = r.GetU32();
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::Mkdir(uint32 dirFid, const char *name, uint32 mode,
	uint32 gid, NineQid &outQid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tmkdir, NewTag());
	w.PutU32(dirFid);
	w.PutString(name);
	w.PutU32(mode);
	w.PutU32(gid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rmkdir));

	r.GetQid(outQid);
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::Symlink(uint32 dirFid, const char *name,
	const char *target, uint32 gid, NineQid &outQid)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tsymlink, NewTag());
	w.PutU32(dirFid);
	w.PutString(name);
	w.PutString(target);
	w.PutU32(gid);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rsymlink));

	r.GetQid(outQid);
	return r.Ok() ? B_OK : B_IO_ERROR;
}


status_t NineConnection::UnlinkAt(uint32 dirFid, const char *name, uint32 flags)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tunlinkat, NewTag());
	w.PutU32(dirFid);
	w.PutString(name);
	w.PutU32(flags);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	return CheckReply(r, NineType::Runlinkat);
}


status_t NineConnection::RenameAt(uint32 oldDirFid, const char *oldName,
	uint32 newDirFid, const char *newName)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Trenameat, NewTag());
	w.PutU32(oldDirFid);
	w.PutString(oldName);
	w.PutU32(newDirFid);
	w.PutString(newName);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	return CheckReply(r, NineType::Rrenameat);
}


status_t NineConnection::SetAttr(uint32 fid, uint32 valid, uint32 mode,
	uint32 uid, uint32 gid, uint64 size, int64 atimeSec, int64 atimeNsec,
	int64 mtimeSec, int64 mtimeNsec)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Tsetattr, NewTag());
	w.PutU32(fid);
	w.PutU32(valid);
	w.PutU32(mode);
	w.PutU32(uid);
	w.PutU32(gid);
	w.PutU64(size);
	w.PutU64((uint64)atimeSec);
	w.PutU64((uint64)atimeNsec);
	w.PutU64((uint64)mtimeSec);
	w.PutU64((uint64)mtimeNsec);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	return CheckReply(r, NineType::Rsetattr);
}


status_t NineConnection::Write(uint32 fid, uint64 offset, uint32 count,
	const void *buffer, uint32 &outCount)
{
	MutexLocker locker(&fLock);

	NineWriter w(&fSendBuf[0], fMsize, NineType::Twrite, NewTag());
	w.PutU32(fid);
	w.PutU64(offset);
	w.PutU32(count);
	w.PutData(buffer, count);
	size_t tlen = w.Finish();
	if (tlen == 0)
		return B_BUFFER_OVERFLOW;

	size_t rlen;
	CHECK_RET(Transceive(tlen, rlen));

	NineReader r(&fRecvBuf[0], rlen);
	CHECK_RET(CheckReply(r, NineType::Rwrite));

	outCount = r.GetU32();
	return r.Ok() && outCount <= count ? B_OK : B_IO_ERROR;
}
