## Haiku Generic Makefile v2.6 ##

# The name of the binary.
NAME = 9pfs

# The type of binary, must be one of:
#	APP:	Application
#	SHARED:	Shared library or add-on
#	STATIC:	Static library archive
#	DRIVER: Kernel driver
TYPE = DRIVER

APP_MIME_SIG =

#%{
# @src->@

SRCS = \
	kernel_interface.cpp \
	NinePVolume.cpp \
	NinePVnode.cpp \
	NinePFileVnode.cpp \
	NinePDirVnode.cpp \
	NinePSymlinkVnode.cpp \
	NineConnection.cpp \
	NineMessage.cpp \
	ExternalAllocator.cpp \

RDEFS =

RSRCS =

# @<-src@
#%}

LIBS =

LIBPATHS =

SYSTEM_INCLUDE_PATHS = \
	/boot/system/develop/headers/private \
	/boot/system/develop/headers/private/shared \
	/boot/system/develop/headers/private/kernel \
	/boot/system/develop/headers/private/kernel/arch/x86 \
	/boot/system/develop/headers/private/system \
	/boot/system/develop/headers/private/system/arch/x86_64 \

# The 9pfs and virtio_9p add-ons share NineTransport.h from the transport dir.
LOCAL_INCLUDE_PATHS = \
	../transport \

OPTIMIZE :=

LOCALES =

DEFINES =

WARNINGS =

SYMBOLS :=

DEBUGGER :=

COMPILER_FLAGS = -fvisibility=hidden -fno-exceptions

LINKER_FLAGS =

APP_VERSION :=

DRIVER_PATH =

## Include the Makefile-Engine
DEVEL_DIRECTORY := \
	$(shell findpaths -r "makefile_engine" B_FIND_PATH_DEVELOP_DIRECTORY)
include $(DEVEL_DIRECTORY)/etc/makefile-engine
