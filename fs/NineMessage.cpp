#include "NineMessage.h"

#include <string.h>


//#pragma mark - NineWriter

NineWriter::NineWriter(void *buf, size_t cap, NineType type, uint16 tag):
	fBuf(static_cast<uint8*>(buf)),
	fCap(cap)
{
	// Reserve the size[4] field; it is filled in by Finish().
	PutU32(0);
	PutU8(static_cast<uint8>(type));
	PutU16(tag);
}


void NineWriter::PutU8(uint8 value)
{
	if (fPos + 1 > fCap) {
		fError = true;
		return;
	}
	fBuf[fPos++] = value;
}


void NineWriter::PutU16(uint16 value)
{
	PutU8((uint8)(value >> 0));
	PutU8((uint8)(value >> 8));
}


void NineWriter::PutU32(uint32 value)
{
	PutU16((uint16)(value >> 0));
	PutU16((uint16)(value >> 16));
}


void NineWriter::PutU64(uint64 value)
{
	PutU32((uint32)(value >> 0));
	PutU32((uint32)(value >> 32));
}


void NineWriter::PutString(const char *str)
{
	size_t length = str == nullptr ? 0 : strlen(str);
	if (length > 0xffff) {
		fError = true;
		return;
	}
	PutU16((uint16)length);
	PutData(str, length);
}


void NineWriter::PutData(const void *data, size_t length)
{
	if (length == 0)
		return;
	if (fPos + length > fCap) {
		fError = true;
		return;
	}
	memcpy(fBuf + fPos, data, length);
	fPos += length;
}


size_t NineWriter::Finish()
{
	if (fError)
		return 0;
	uint32 size = (uint32)fPos;
	fBuf[0] = (uint8)(size >> 0);
	fBuf[1] = (uint8)(size >> 8);
	fBuf[2] = (uint8)(size >> 16);
	fBuf[3] = (uint8)(size >> 24);
	return fPos;
}


//#pragma mark - NineReader

NineReader::NineReader(const void *buf, size_t length):
	fBuf(static_cast<const uint8*>(buf)),
	fLen(length)
{
	if (length < kNineHeaderSize)
		fError = true;
}


NineType NineReader::Type() const
{
	return static_cast<NineType>(fBuf[4]);
}


uint16 NineReader::Tag() const
{
	return (uint16)fBuf[5] | ((uint16)fBuf[6] << 8);
}


uint8 NineReader::GetU8()
{
	if (fError || fPos + 1 > fLen) {
		fError = true;
		return 0;
	}
	return fBuf[fPos++];
}


uint16 NineReader::GetU16()
{
	uint16 low = GetU8();
	uint16 high = GetU8();
	return low | (high << 8);
}


uint32 NineReader::GetU32()
{
	uint32 low = GetU16();
	uint32 high = GetU16();
	return low | (high << 16);
}


uint64 NineReader::GetU64()
{
	uint64 low = GetU32();
	uint64 high = GetU32();
	return low | (high << 32);
}


void NineReader::GetQid(NineQid &qid)
{
	qid.type = GetU8();
	qid.version = GetU32();
	qid.path = GetU64();
}


bool NineReader::GetString(const char *&str, uint16 &length)
{
	length = GetU16();
	if (fError || fPos + length > fLen) {
		fError = true;
		return false;
	}
	str = reinterpret_cast<const char*>(fBuf + fPos);
	fPos += length;
	return true;
}


const uint8 *NineReader::GetData(size_t count)
{
	if (fError || fPos + count > fLen) {
		fError = true;
		return nullptr;
	}
	const uint8 *data = fBuf + fPos;
	fPos += count;
	return data;
}
