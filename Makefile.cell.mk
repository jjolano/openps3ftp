# OpenPS3FTP Makefile - CELL edition

ifeq ($(strip $(CELL_SDK)),)
$(error CELL_SDK not defined.)
endif

CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = client.cpp command.cpp server.cpp
PPU_LIB_TARGET = libcellps3ftp.a
PPU_CPPFLAGS += -D_PS3SDK_

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS = -Iftp

include $(CELL_MK_DIR)/sdk.target.mk
