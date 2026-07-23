#include "NinePfs.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <NodeMonitor.h>

#include <new>


int NineTypeToStatType(uint8 qidType)
{
	if ((qidType & kNineQtDir) != 0)
		return S_IFDIR;
	if ((qidType & kNineQtSymlink) != 0)
		return S_IFLNK;
	return S_IFREG;
}


//#pragma mark - NinePVnode

NinePVnode::NinePVnode(NineQid qid, uint32 fid):
	fId((ino_t)qid.path),
	fFid(fid),
	fQid(qid)
{
}


NinePVnode::~NinePVnode()
{
}


status_t NinePVnode::SetName(const char *name)
{
	size_t len = strlen(name) + 1;
	ArrayDeleter<char> newName(new(std::nothrow) char[len]);
	if (!newName.IsSet())
		return B_NO_MEMORY;
	memcpy(&newName[0], name, len);
	fName.SetTo(newName.Detach());
	return B_OK;
}


NineConnection &NinePVnode::Conn() const
{
	return fVolume->Conn();
}


status_t NinePVnode::ReadStat(struct stat &stat)
{
	NineStat nineStat;
	CHECK_RET(Conn().GetAttr(fFid, kNineGetattrBasic, nineStat));

	stat = {
		.st_ino = fId,
		.st_mode = (mode_t)nineStat.mode,
		.st_nlink = (nlink_t)nineStat.nlink,
		.st_uid = (uid_t)nineStat.uid,
		.st_gid = (gid_t)nineStat.gid,
		.st_size = (off_t)nineStat.size,
		.st_atim = { (time_t)nineStat.atimeSec, (long)nineStat.atimeNsec },
		.st_mtim = { (time_t)nineStat.mtimeSec, (long)nineStat.mtimeNsec },
		.st_ctim = { (time_t)nineStat.ctimeSec, (long)nineStat.ctimeNsec },
		.st_crtim = { (time_t)nineStat.mtimeSec, (long)nineStat.mtimeNsec },
		.st_blocks = (blkcnt_t)nineStat.blocks,
	};
	return B_OK;
}


status_t NinePVnode::GetVnodeName(char *buffer, size_t bufferSize)
{
	strlcpy(buffer, Name(), bufferSize);
	return B_OK;
}


status_t NinePVnode::WriteStat(const struct stat &stat, uint32 statMask)
{
	uint32 valid = 0;
	uint32 mode = 0, uid = 0, gid = 0;
	uint64 size = 0;
	int64 atimeSec = 0, atimeNsec = 0, mtimeSec = 0, mtimeNsec = 0;

	if ((statMask & B_STAT_MODE) != 0) {
		valid |= kNineSetattrMode;
		mode = stat.st_mode & 07777;
	}
	if ((statMask & B_STAT_UID) != 0) {
		valid |= kNineSetattrUid;
		uid = stat.st_uid;
	}
	if ((statMask & B_STAT_GID) != 0) {
		valid |= kNineSetattrGid;
		gid = stat.st_gid;
	}
	if ((statMask & B_STAT_SIZE) != 0) {
		valid |= kNineSetattrSize;
		size = (uint64)stat.st_size;
	}
	if ((statMask & B_STAT_ACCESS_TIME) != 0) {
		valid |= kNineSetattrAtime | kNineSetattrAtimeSet;
		atimeSec = stat.st_atim.tv_sec;
		atimeNsec = stat.st_atim.tv_nsec;
	}
	if ((statMask & B_STAT_MODIFICATION_TIME) != 0) {
		valid |= kNineSetattrMtime | kNineSetattrMtimeSet;
		mtimeSec = stat.st_mtim.tv_sec;
		mtimeNsec = stat.st_mtim.tv_nsec;
	}

	if (valid == 0)
		return B_OK;

	CHECK_RET(Conn().SetAttr(fFid, valid, mode, uid, gid, size,
		atimeSec, atimeNsec, mtimeSec, mtimeNsec));

	if ((statMask & B_STAT_SIZE) != 0)
		OnSizeChanged((off_t)size);

	notify_stat_changed(Volume()->Id(), -1, Id(), statMask);
	return B_OK;
}


status_t NinePVnode::Access(int mode)
{
	// Access enforcement is left to the host; failures surface at the operation
	// level via the server's Rlerror.
	return B_OK;
}


//#pragma mark - default op implementations

status_t NinePVnode::Lookup(const char *name, ino_t &id)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::Open(int openMode, NinePFileCookie *&cookie)
{
	cookie = nullptr;
	return B_OK;
}


status_t NinePVnode::Close(NinePFileCookie *cookie)
{
	return B_OK;
}


status_t NinePVnode::FreeCookie(NinePFileCookie *cookie)
{
	return B_OK;
}


status_t NinePVnode::Read(NinePFileCookie *cookie, off_t pos, void *buffer, size_t &length)
{
	return B_IS_A_DIRECTORY;
}


status_t NinePVnode::Write(NinePFileCookie *cookie, off_t pos, const void *buffer, size_t &length)
{
	return B_IS_A_DIRECTORY;
}


status_t NinePVnode::OpenDir(NinePDirCookie *&cookie)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::CloseDir(NinePDirCookie *cookie)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::FreeDirCookie(NinePDirCookie *cookie)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::ReadDir(NinePDirCookie *cookie, struct dirent *buffer, size_t bufferSize, uint32 &num)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::RewindDir(NinePDirCookie *cookie)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::ReadSymlink(char *buffer, size_t &bufferSize)
{
	return B_BAD_VALUE;
}


status_t NinePVnode::Create(const char *name, int openMode, int perms, NinePFileCookie *&cookie, ino_t &newVnodeID)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::CreateDir(const char *name, int perms)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::CreateSymlink(const char *name, const char *path, int mode)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::Unlink(const char *name)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::RemoveDir(const char *name)
{
	return B_NOT_A_DIRECTORY;
}


status_t NinePVnode::Rename(const char *fromName, NinePVnode *toDir, const char *toName)
{
	return B_NOT_A_DIRECTORY;
}


uint32 NineHaikuToLinuxOpenFlags(int openMode)
{
	// Only the access mode and a few positional flags share meaning; their
	// numeric encodings differ between Haiku and Linux, so translate explicitly.
	uint32 flags = 0;
	switch (openMode & O_ACCMODE) {
		case O_WRONLY:
			flags = 1;
			break;
		case O_RDWR:
			flags = 2;
			break;
		default:
			flags = 0;
			break;
	}
	if ((openMode & O_APPEND) != 0)
		flags |= 02000;
	if ((openMode & O_TRUNC) != 0)
		flags |= 01000;
	if ((openMode & O_NONBLOCK) != 0)
		flags |= 04000;
	return flags;
}
