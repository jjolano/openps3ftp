# libCellPS3FTP Makefile
GIT_VERSION := $(shell git describe --abbrev=5 --dirty --always --tags)

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = $(wildcard *.c)
PPU_CFLAGS += -DAPP_VERSION=\"$(GIT_VERSION)\"
PPU_LIB_TARGET = libopenps3ftp_vsh.a

ifeq ($(LIB_EXTERNAL), 1)
PPU_CFLAGS += -D_NO_VSH_EXPORTS_
PPU_LIB_TARGET = libopenps3ftp.a
endif

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS += -I../../include/prx -I../../include/prx/ftp

include $(CELL_MK_DIR)/sdk.target.mk
