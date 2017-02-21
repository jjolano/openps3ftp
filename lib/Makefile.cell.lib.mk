# libCellPS3FTP Makefile

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = $(wildcard *.cpp)
PPU_LIB_TARGET = libcellps3ftp.a
PPU_CPPFLAGS += -DCELL_SDK -fno-exceptions -fno-rtti

PPU_OPTIMIZE_LV = -O2
PPU_INCDIRS += -I../include

include $(CELL_MK_DIR)/sdk.target.mk
