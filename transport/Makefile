## Haiku Generic Makefile v2.6 ##

# The name of the binary.
NAME = virtio_9p

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
	Virtio9pDevice.cpp \
	Virtio9pHandle.cpp \

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
	/boot/system/develop/headers/private/drivers \
	/boot/system/develop/headers/private/virtio \

LOCAL_INCLUDE_PATHS =

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
