# OpenPS3FTP Makefile - CELL edition

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

GCM_UTIL = $(CELL_COMMON_DIR)/gcmutil

PPU_SRCS = $(GCM_UTIL)/EntryPoint.c $(wildcard cell/*.cpp) $(wildcard helper/*.cpp) $(wildcard feat/*/*.cpp)
PPU_TARGET = cellps3ftp.elf

SUBDIRS = $(GCM_UTIL)

PPU_LDFLAGS += -Wl,--strip-unused
PPU_LDLIBDIR += -L../lib
PPU_LDLIBS += -lcellps3ftp -lnetctl_stub -lnet_stub -lgcm_cmd -lgcm_sys_stub -lfs_stub -lsysutil_stub -lsysmodule_stub -llv2_stub -ldbgfont_gcm
PPU_LIBS += $(GCM_UTIL)/gcmutil.a

PPU_OPTIMIZE_LV = -O2
PPU_INCDIRS += -I$(GCM_UTIL) -I../include -I./helper

GCC_PPU_CXXFLAGS += -fno-exceptions -fno-rtti

include $(CELL_MK_DIR)/sdk.target.mk
