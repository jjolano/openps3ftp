# OpenPS3FTP Makefile - CELL PRX edition
GIT_VERSION := $(shell git describe --dirty --always)

SCETOOL_FLAGS := -0 SELF
SCETOOL_FLAGS += -1 TRUE
SCETOOL_FLAGS += -s FALSE
SCETOOL_FLAGS += -2 04
SCETOOL_FLAGS += -3 1070000052000001
SCETOOL_FLAGS += -4 01000002
SCETOOL_FLAGS += -5 APP
SCETOOL_FLAGS += -A 0001000000000000
SCETOOL_FLAGS += -6 0003004000000000

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

OBJS_DIR = .

PPU_SRCS = $(wildcard util/*.c) $(wildcard ../../feat/*/*.c) $(wildcard *.c)
PPU_PRX_TARGET = openps3ftp.prx
PPU_CFLAGS += -Wno-unused-parameter -ffunction-sections -fdata-sections -fno-builtin-printf -std=gnu99
PPU_CFLAGS += -DCELL_SDK -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
PPU_CFLAGS += -D_FILE_OFFSET_BITS=64

PPU_PRX_LDFLAGS += -Wl,--strip-unused-data
PPU_PRX_LDFLAGS += -nodefaultlibs

PPU_PRX_LDLIBDIR += -L../../lib -Lvsh/lib

PPU_PRX_LDLIBS += -lopenps3ftp_cell_vsh -lfs_stub -llv2_stub
PPU_PRX_LDLIBS += -lsys_net_export_stub -lallocator_export_stub -lstdc_export_stub -lsysPrxForUser_export_stub \
				-lvshtask_export_stub

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS += -I. -Ivsh/include -Iinclude -I../../include -I../../feat

ifeq ($(NTFS_SUPPORT), 1)
PPU_CFLAGS += -D_NTFS_SUPPORT_
PPU_INCDIRS += -I../../external/ps3ntfs/external/libntfs/include
PPU_INCDIRS += -I../../external/ps3ntfs

ifeq ($(NTFS_STATIC), 1)
PPU_SRCS += ../../external/ps3ntfs/ps3ntfs.c

PPU_PRX_LDLIBDIR += -L../../external/ps3ntfs/external/libntfs
PPU_PRX_LDLIBS += -lntfs_prx
else
PPU_CFLAGS += -D_PS3NTFS_PRX_

PPU_PRX_LDLIBDIR += -L../../external/ps3ntfs
PPU_PRX_LDLIBS += -lps3ntfs_prx
endif
endif

all: $(PPU_PRX_TARGET)
	$(PPU_PRX_STRIP) --strip-debug --strip-section-header $<
	scetool $(SCETOOL_FLAGS) -e $< $(PPU_SPRX_TARGET)

include $(CELL_MK_DIR)/sdk.target.mk
