#pragma once

#include <SupportDefs.h>


// 9P2000.L wire protocol constants (the Linux dialect, as spoken by QEMU's
// virtio-9p). Message layout is: size[4] type[1] tag[2] body, all little-endian;
// strings are len[2] followed by len bytes with no terminator.

enum {
	kNineHeaderSize = 7,
	kNineNoFid = 0xffffffff,
	kNineNoTag = 0xffff,
	kNineNoNUname = 0xffffffff,
};


enum class NineType : uint8 {
	Rlerror   = 7,
	Tstatfs   = 8,   Rstatfs   = 9,
	Tlopen    = 12,  Rlopen    = 13,
	Tlcreate  = 14,  Rlcreate  = 15,
	Tsymlink  = 16,  Rsymlink  = 17,
	Tmknod    = 18,  Rmknod    = 19,
	Treadlink = 22,  Rreadlink = 23,
	Tgetattr  = 24,  Rgetattr  = 25,
	Tsetattr  = 26,  Rsetattr  = 27,
	Treaddir  = 40,  Rreaddir  = 41,
	Tmkdir    = 72,  Rmkdir    = 73,
	Trenameat = 74,  Rrenameat = 75,
	Tunlinkat = 76,  Runlinkat = 77,
	Tversion  = 100, Rversion  = 101,
	Tattach   = 104, Rattach   = 105,
	Tflush    = 108, Rflush    = 109,
	Twalk     = 110, Rwalk     = 111,
	Tread     = 116, Rread     = 117,
	Twrite    = 118, Rwrite    = 119,
	Tclunk    = 120, Rclunk    = 121,
	Tremove   = 122, Rremove   = 123,
};


// qid.type bits.
enum {
	kNineQtDir     = 0x80,
	kNineQtSymlink = 0x02,
	kNineQtFile    = 0x00,
};


// Tgetattr request_mask / Rgetattr valid bits.
enum {
	kNineGetattrMode   = 0x00000001,
	kNineGetattrNlink  = 0x00000002,
	kNineGetattrUid    = 0x00000004,
	kNineGetattrGid    = 0x00000008,
	kNineGetattrRdev   = 0x00000010,
	kNineGetattrAtime  = 0x00000020,
	kNineGetattrMtime  = 0x00000040,
	kNineGetattrCtime  = 0x00000080,
	kNineGetattrIno    = 0x00000100,
	kNineGetattrSize   = 0x00000200,
	kNineGetattrBlocks = 0x00000400,
	kNineGetattrBasic  = 0x000007ff,
	kNineGetattrAll    = 0x00003fff,
};


// Tsetattr valid bits.
enum {
	kNineSetattrMode     = 0x00000001,
	kNineSetattrUid      = 0x00000002,
	kNineSetattrGid      = 0x00000004,
	kNineSetattrSize     = 0x00000008,
	kNineSetattrAtime    = 0x00000010,
	kNineSetattrMtime    = 0x00000020,
	kNineSetattrCtime    = 0x00000040,
	kNineSetattrAtimeSet = 0x00000080,
	kNineSetattrMtimeSet = 0x00000100,
};


// Tunlinkat flags (Linux AT_REMOVEDIR).
enum {
	kNineAtRemoveDir = 0x200,
};


struct NineQid {
	uint8 type;
	uint32 version;
	uint64 path;
};


// Decoded Rgetattr reply (only the fields 9pfs consumes).
struct NineStat {
	NineQid qid;
	uint32 mode;
	uint32 uid;
	uint32 gid;
	uint64 nlink;
	uint64 size;
	uint64 blocks;
	int64 atimeSec;
	int64 atimeNsec;
	int64 mtimeSec;
	int64 mtimeNsec;
	int64 ctimeSec;
	int64 ctimeNsec;
};
