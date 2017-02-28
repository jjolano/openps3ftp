# libCellPS3FTP Makefile

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = $(wildcard *.c)
PPU_LIB_TARGET = libprxftp.a

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS += -I../../include/prx -I../../include/prx/ftp

include $(CELL_MK_DIR)/sdk.target.mk
