#include "NinePfs.h"

#include <string.h>


//#pragma mark - NinePSymlinkVnode

status_t NinePSymlinkVnode::ReadSymlink(char *buffer, size_t &bufferSize)
{
	if (bufferSize == 0)
		return B_BAD_VALUE;

	CHECK_RET(Conn().ReadLink(Fid(), buffer, bufferSize));
	bufferSize = strlen(buffer);
	return B_OK;
}
