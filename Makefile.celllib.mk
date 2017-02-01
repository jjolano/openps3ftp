# OpenPS3FTP Makefile - CELL edition

CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = client.cpp command.cpp server.cpp
PPU_LIB_TARGET = lib$(NAME).a
PPU_CPPFLAGS += -D_PS3SDK_

PPU_OPTIMIZE_LV = -O2
PPU_INCDIRS += -I. -Iftp

include $(CELL_MK_DIR)/sdk.target.mk
