CELL_SDK ?= /c/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk

include $(CELL_MK_DIR)/sdk.makedef.mk

BUILD_TYPE = release

BUILD_TAG = libntfs_prx
PPU_LIB_TARGET	= libntfs_prx.a
PPU_OPTIMIZE_LV := -Os

PPU_INCDIRS = -Iinclude -Isource

#PPU_SRCS = $(wildcard source/*.c)
#PPU_SRCS += $(wildcard source/libext2fs/*.c)

PPU_SRCS += \
	source/acls.c \
	source/attrib.c \
	source/attrlist.c \
	source/bitmap.c \
	source/bootsect.c \
	source/cache.c \
	source/cache2.c \
	source/collate.c \
	source/compat.c \
	source/compress.c \
	source/debug.c \
	source/device.c \
	source/device_io.c \
	source/dir.c \
	source/efs.c \
	source/gekko_io.c \
	source/index.c \
	source/inode.c \
	source/lcnalloc.c \
	source/logfile.c \
	source/logging.c \
	source/mft.c \
	source/misc.c \
	source/mst.c \
	source/ntfs.c \
	source/ntfsdir.c \
	source/ntfsfile.c \
	source/ntfsinternal.c \
	source/object_id.c \
	source/realpath.c \
	source/reent.c \
	source/reparse.c \
	source/runlist.c \
	source/security.c \
	source/unistr.c \
	source/volume.c \
	source/xattrs.c \

#DEFINES += -DWITH_EXT_SUPPORT
#DEFINES += -DPS3_STDIO
DEFINES += -DBIGENDIAN -DPS3_GEKKO -DHAVE_CONFIG_H -DPRX

#PPU_CFLAGS := -Os -Wall -mcpu=cell -fno-strict-aliasing $(PPU_INCDIRS) $(DEFINES) -ffunction-sections -fdata-sections -fno-builtin-printf -nodefaultlibs -std=gnu99 -Wno-shadow -Wno-unused-parameter
PPU_CFLAGS := -Os -Wall -std=gnu99 -mcpu=cell -fno-strict-aliasing $(PPU_INCDIRS) $(DEFINES)

ifeq ($(BUILD_TYPE), debug)
PPU_CFLAGS 	+= -DDEBUG
PPU_LIB_TARGET	= libntfs_prx.debug.a
endif

include $(CELL_MK_DIR)/sdk.target.mk
