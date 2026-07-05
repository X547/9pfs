#pragma once

#include <lock.h>
#include <AutoDeleter.h>

#include "NineProtocol.h"
#include "NineMessage.h"
#include "ExternalAllocator.h"


// A live 9P2000.L session over a virtio_9p transport device. Owns the transport
// fd, the negotiated msize, the send/receive buffers, and the fid pool. Every
// typed request serializes on fLock (one in-flight exchange, matching the
// transport), builds a T-message, calls the transceive ioctl, and parses the
// reply — translating Rlerror into a Haiku status_t.

class NineConnection {
private:
	int fFd = -1;
	uint32 fMsize = 0;

	ArrayDeleter<uint8> fSendBuf;
	ArrayDeleter<uint8> fRecvBuf;

	mutex fLock = MUTEX_INITIALIZER("9p connection");
	ExternalAllocator fFidPool;

	// Serialized transceive: send fSendBuf[0..tlen), receive into fRecvBuf.
	status_t Transceive(size_t tlen, size_t &rlen);
	status_t CheckReply(NineReader &reader, NineType expected);

public:
	NineConnection();
	~NineConnection();

	status_t Init(const char *devicePath, uint32 &transportMsize, char *mountTag,
		size_t mountTagSize);

	uint32 Msize() const {return fMsize;}

	bool AllocFid(uint32 &fid);
	void FreeFid(uint32 fid);

	status_t Version(uint32 wantMsize);
	status_t Attach(uint32 fid, const char *uname, const char *aname, NineQid &outQid);
	status_t WalkClone(uint32 fid, uint32 newFid);
	status_t Walk(uint32 fid, uint32 newFid, const char *name, NineQid &outQid);
	status_t GetAttr(uint32 fid, uint64 mask, NineStat &outStat);
	status_t Lopen(uint32 fid, uint32 flags, NineQid &outQid, uint32 &outIounit);
	status_t Read(uint32 fid, uint64 offset, uint32 count, void *buffer, uint32 &outCount);
	status_t ReadDir(uint32 fid, uint64 offset, uint32 count, void *buffer, uint32 &outLength);
	status_t ReadLink(uint32 fid, char *buffer, size_t bufferSize);
	status_t Clunk(uint32 fid);
};
