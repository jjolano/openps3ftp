# ps3ntfs sprx Makefile
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

PPU_SRCS = $(wildcard util/*.c) $(wildcard *.c)
PPU_PRX_TARGET = ps3ntfs.prx
PPU_CFLAGS += -ffunction-sections -fdata-sections -fno-builtin-printf -std=gnu99 -Wno-shadow -Wno-unused-parameter

PPU_PRX_LDFLAGS += -Wl,--strip-unused-data
PPU_PRX_LDLIBDIR += -L./external/libntfs -L./vsh/lib

PPU_PRX_LDLIBS += -lstdc_export_stub -lallocator_export_stub -lsysPrxForUser_export_stub -lvshtask_export_stub
PPU_PRX_LDLIBS += -lntfs_prx

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS += -I./include -I./external/libntfs/include -I./vsh/include

all: $(PPU_PRX_TARGET)
	$(PPU_PRX_STRIP) --strip-debug --strip-section-header $<
	scetool $(SCETOOL_FLAGS) -e $< $(PPU_SPRX_TARGET)

include $(CELL_MK_DIR)/sdk.target.mk
