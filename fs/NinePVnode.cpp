#include "NinePfs.h"

#include <sys/stat.h>

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


status_t NinePVnode::Access(int mode)
{
	// Access enforcement is left to the host; read-only mounts reject writes at
	// the operation level.
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
