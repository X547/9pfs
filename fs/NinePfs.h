#pragma once

#pragma GCC diagnostic ignored "-Winvalid-offsetof"

#include <fs_interface.h>
#include <lock.h>
#include <AutoDeleter.h>
#include <util/AVLTree.h>

#include <string.h>

#include "NineTransport.h"
#include "NineProtocol.h"
#include "NineConnection.h"


#if 0
#define TRACE(x...) dprintf(x)
#else
#define TRACE(x...) ;
#endif

#define CHECK_RET(err) {status_t _err = (err); if (_err < B_OK) return _err;}


class NinePVolume;
class NinePVnode;


// Open cookie for a regular file: a separate opened fid cloned from the vnode's
// primary fid (which must stay unopened for walking), plus the server iounit.
struct NinePFileCookie {
	uint32 openFid = kNineNoFid;
	uint32 iounit = 0;
	int openMode = 0;
};


// Open cookie for a directory iteration: the opened fid, the next 9P read
// offset, and a buffer holding the current Treaddir chunk being parsed.
struct NinePDirCookie {
	uint32 openFid = kNineNoFid;
	uint64 nextOffset = 0;
	ArrayDeleter<uint8> chunk;
	uint32 chunkLength = 0;
	uint32 chunkPos = 0;
	bool eof = false;
};


class NinePVnode {
private:
	friend class NinePVolume;

	NinePVolume *fVolume {};
	ino_t fId = 0;
	uint32 fFid = kNineNoFid;
	NineQid fQid {};
	ArrayDeleter<char> fName;
	AVLTreeNode fIdNode;

	struct IdNodeDef {
		typedef ino_t Key;
		typedef NinePVnode Value;

		inline AVLTreeNode *GetAVLTreeNode(Value *value) const
		{
			return &value->fIdNode;
		}

		inline Value *GetValue(AVLTreeNode *node) const
		{
			return (Value*)((char*)node - offsetof(Value, fIdNode));
		}

		inline int Compare(const Key &a, const Value *b) const
		{
			return (a < b->fId) ? -1 : (a > b->fId) ? 1 : 0;
		}

		inline int Compare(const Value *a, const Value *b) const
		{
			return (a->fId < b->fId) ? -1 : (a->fId > b->fId) ? 1 : 0;
		}
	};

public:
	typedef AVLTree<IdNodeDef> IdMap;

	NinePVnode(NineQid qid, uint32 fid);
	virtual ~NinePVnode();

	inline ino_t Id() const {return fId;}
	inline uint32 Fid() const {return fFid;}
	inline NinePVolume *Volume() const {return fVolume;}
	inline const NineQid &Qid() const {return fQid;}
	inline const char *Name() const {return !fName.IsSet() ? "" : &fName[0];}
	status_t SetName(const char *name);

	NineConnection &Conn() const;

	status_t ReadStat(struct stat &stat);
	status_t WriteStat(const struct stat &stat, uint32 statMask);
	status_t GetVnodeName(char *buffer, size_t bufferSize);
	status_t Access(int mode);

	// Hook so a size change from write_stat (truncate) can resize a file cache.
	virtual void OnSizeChanged(off_t newSize) {}

	virtual status_t Lookup(const char *name, ino_t &id);
	virtual status_t Open(int openMode, NinePFileCookie *&cookie);
	virtual status_t Close(NinePFileCookie *cookie);
	virtual status_t FreeCookie(NinePFileCookie *cookie);
	virtual status_t Read(NinePFileCookie *cookie, off_t pos, void *buffer, size_t &length);
	virtual status_t Write(NinePFileCookie *cookie, off_t pos, const void *buffer, size_t &length);
	virtual status_t OpenDir(NinePDirCookie *&cookie);
	virtual status_t CloseDir(NinePDirCookie *cookie);
	virtual status_t FreeDirCookie(NinePDirCookie *cookie);
	virtual status_t ReadDir(NinePDirCookie *cookie, struct dirent *buffer, size_t bufferSize, uint32 &num);
	virtual status_t RewindDir(NinePDirCookie *cookie);
	virtual status_t ReadSymlink(char *buffer, size_t &bufferSize);
	virtual status_t Create(const char *name, int openMode, int perms, NinePFileCookie *&cookie, ino_t &newVnodeID);
	virtual status_t CreateDir(const char *name, int perms);
	virtual status_t CreateSymlink(const char *name, const char *path, int mode);
	virtual status_t Unlink(const char *name);
	virtual status_t RemoveDir(const char *name);
	virtual status_t Rename(const char *fromName, NinePVnode *toDir, const char *toName);
};


class NinePFileVnode: public NinePVnode {
private:
	void *fFileCache = nullptr;
	uint32 fCacheFid = kNineNoFid;
	off_t fCacheSize = 0;

	// Lazily creates the page cache and a dedicated opened fid for page I/O,
	// so mmap and executable loading work. Safe to call repeatedly.
	status_t EnsureCache();

public:
	using NinePVnode::NinePVnode;
	~NinePFileVnode();

	status_t Open(int openMode, NinePFileCookie *&cookie) final;
	status_t Close(NinePFileCookie *cookie) final;
	status_t FreeCookie(NinePFileCookie *cookie) final;
	status_t Read(NinePFileCookie *cookie, off_t pos, void *buffer, size_t &length) final;
	status_t Write(NinePFileCookie *cookie, off_t pos, const void *buffer, size_t &length) final;
	void OnSizeChanged(off_t newSize) final;

	status_t ReadPages(off_t pos, const struct iovec *vecs, size_t count, size_t &numBytes);
	status_t WritePages(off_t pos, const struct iovec *vecs, size_t count, size_t &numBytes);
};


class NinePDirVnode: public NinePVnode {
private:
	// Walks name to obtain the child's inode number (for notifications and
	// vnode removal), leaving no lingering fid.
	status_t ResolveChild(const char *name, ino_t &id);

public:
	using NinePVnode::NinePVnode;

	status_t Lookup(const char *name, ino_t &id) final;
	status_t OpenDir(NinePDirCookie *&cookie) final;
	status_t CloseDir(NinePDirCookie *cookie) final;
	status_t FreeDirCookie(NinePDirCookie *cookie) final;
	status_t ReadDir(NinePDirCookie *cookie, struct dirent *buffer, size_t bufferSize, uint32 &num) final;
	status_t RewindDir(NinePDirCookie *cookie) final;
	status_t Create(const char *name, int openMode, int perms, NinePFileCookie *&cookie, ino_t &newVnodeID) final;
	status_t CreateDir(const char *name, int perms) final;
	status_t CreateSymlink(const char *name, const char *path, int mode) final;
	status_t Unlink(const char *name) final;
	status_t RemoveDir(const char *name) final;
	status_t Rename(const char *fromName, NinePVnode *toDir, const char *toName) final;
};


class NinePSymlinkVnode: public NinePVnode {
public:
	using NinePVnode::NinePVnode;

	status_t ReadSymlink(char *buffer, size_t &bufferSize) final;
};


class NinePVolume {
private:
	friend class NinePVnode;

	recursive_lock fLock = RECURSIVE_LOCK_INITIALIZER("9pfs volume");

	fs_volume *fBase {};
	NineConnection fConn;
	NinePVnode *fRootVnode {};
	NinePVnode::IdMap fIds;
	char fVolumeName[kNineMaxTagLen] {};

public:
	NinePVolume() = default;

	inline recursive_lock *Lock() {return &fLock;}
	inline fs_volume *Base() {return fBase;}
	inline dev_t Id() {return fBase->id;}
	inline NineConnection &Conn() {return fConn;}

	// Creates a vnode of the right subclass for a qid and inserts it into fIds.
	NinePVnode *GetOrCreateVnode(const NineQid &qid, uint32 fid, const char *name);
	NinePVnode *FindVnode(ino_t id);
	void RemoveVnode(NinePVnode *vnode);

	static status_t Mount(NinePVolume *&volume, fs_volume *base, const char *device, uint32 flags, const char *args, ino_t &rootVnodeID);
	status_t Unmount();
	status_t ReadFsInfo(struct fs_info &info);
	status_t GetVnode(ino_t id, NinePVnode *&vnode, int &type, uint32 &flags, bool reenter);
};


int NineTypeToStatType(uint8 qidType);
uint32 NineHaikuToLinuxOpenFlags(int openMode);
