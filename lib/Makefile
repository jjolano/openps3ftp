# libOpenPS3FTP Makefile
SDK ?= linux
GIT_VERSION := $(shell git describe --dirty --always)

ifeq ($(SDK), cell)
CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk

include $(CELL_MK_DIR)/sdk.makedef.mk

OBJS_DIR = .
PPU_CFLAGS += -DCELL_SDK

ifeq ($(LIB_EXTERNAL), 1)
PPU_LIB_TARGET = libopenps3ftp_cell.a
PPU_OPTIMIZE_LV = -O2
else
PPU_LIB_TARGET = libopenps3ftp_cell_vsh.a
PPU_OPTIMIZE_LV = -Os
endif
endif

ifeq ($(SDK), psl1ght)
include $(PSL1GHT)/ppu_rules

PPU_INCDIRS += -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)
PPU_CFLAGS += -DPSL1GHT_SDK -Wall -mregnames -mcpu=cell $(MACHDEP)

ifeq ($(LIB_EXTERNAL), 1)
PPU_LIB_TARGET = libopenps3ftp_psl1ght.a
PPU_OPTIMIZE_LV = -O2
else
PPU_LIB_TARGET = libopenps3ftp_psl1ght_vsh.a
PPU_OPTIMIZE_LV = -Os
endif
endif

ifeq ($(SDK), linux)
PPU_CFLAGS += -DLINUX -g -Wall
PPU_LIB_TARGET = libftp.a
PPU_OPTIMIZE_LV = -Og
endif

PPU_SRCS := $(wildcard *.c)
PPU_OBJS = $(PPU_SRCS:.c=.$(SDK).o)
PPU_CFLAGS += -ffunction-sections -fdata-sections -fno-builtin-printf -DAPP_VER_MIN=\"-g$(GIT_VERSION)\" -std=gnu99

PPU_INCDIRS += -I../include -I../bin/prx/vsh/include -I../bin/prx/include

ifeq ($(LIB_EXTERNAL), 1)
PPU_CFLAGS += -D_NO_VSH_EXPORTS_
endif

ifeq ($(NTFS_SUPPORT), 1)
PPU_CFLAGS += -D_NTFS_SUPPORT_
PPU_INCDIRS += -I../external/ps3ntfs/external/libntfs/include
PPU_INCDIRS += -I../external/ps3ntfs

ifneq ($(NTFS_STATIC), 1)
PPU_CFLAGS += -D_PS3NTFS_PRX_
endif
endif

ifeq ($(SDK), cell)
include $(CELL_MK_DIR)/sdk.target.mk
else
.PHONY: all clean

all: $(PPU_LIB_TARGET)

clean:
	rm -f $(PPU_OBJS) $(PPU_LIB_TARGET)

$(PPU_LIB_TARGET): $(PPU_OBJS)
	$(AR) rcs $@ $^

%.$(SDK).o: %.c
	$(CC) $(PPU_OPTIMIZE_LV) $(PPU_CFLAGS) $(PPU_INCDIRS) -c $< -o $@
endif
