#pragma once

#include "NineProtocol.h"


// Little-endian 9P message codec. NineWriter builds a T-message into a caller
// buffer (header first, then body); Finish() back-patches the size field.
// NineReader parses an R-message with a sticky overflow flag so callers can
// decode optimistically and check Ok() once at the end.

class NineWriter {
private:
	uint8 *fBuf;
	size_t fCap;
	size_t fPos = 0;
	bool fError = false;

public:
	NineWriter(void *buf, size_t cap, NineType type, uint16 tag);

	void PutU8(uint8 value);
	void PutU16(uint16 value);
	void PutU32(uint32 value);
	void PutU64(uint64 value);
	void PutString(const char *str);
	void PutData(const void *data, size_t length);

	bool Ok() const {return !fError;}

	// Writes the total length into the size field and returns it, or 0 on error.
	size_t Finish();
};


class NineReader {
private:
	const uint8 *fBuf;
	size_t fLen;
	size_t fPos = kNineHeaderSize;
	bool fError = false;

public:
	NineReader(const void *buf, size_t length);

	NineType Type() const;
	uint16 Tag() const;

	uint8 GetU8();
	uint16 GetU16();
	uint32 GetU32();
	uint64 GetU64();
	void GetQid(NineQid &qid);

	// Returns a pointer into the message buffer (not null-terminated).
	bool GetString(const char *&str, uint16 &length);
	const uint8 *GetData(size_t count);

	size_t Remaining() const {return fError ? 0 : fLen - fPos;}
	size_t Position() const {return fPos;}
	bool Ok() const {return !fError;}
};
