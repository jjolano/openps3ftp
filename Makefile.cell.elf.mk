# OpenPS3FTP Makefile - CELL edition

CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = main_cell.cpp
PPU_TARGET = $(NAME).elf

PPU_LDFLAGS += -s
PPU_LDLIBS = -l$(NAME) -lnetctl_stub -lnet_stub -lio_stub -lgcm_cmd -lgcm_sys_stub -lfs_stub -lsysutil_stub -lsysmodule_stub -llv2_stub

PPU_OPTIMIZE_LV = -O2
PPU_INCDIRS += -I. -Iftp

include $(CELL_MK_DIR)/sdk.target.mk
