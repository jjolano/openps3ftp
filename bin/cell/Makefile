# OpenPS3FTP Makefile - CELL edition

CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

GCM_UTIL = $(CELL_COMMON_DIR)/gcmutil

PPU_SRCS = main.cpp $(GCM_UTIL)/EntryPoint.c
PPU_TARGET = ftp.elf

SUBDIRS = $(GCM_UTIL)

PPU_LDFLAGS += -Wl,--strip-unused
PPU_LDLIBS += -lopenps3ftp_cell -lnetctl_stub -lnet_stub -lgcm_cmd -lgcm_sys_stub -lfs_stub -lsysutil_stub -lsysmodule_stub -llv2_stub -ldbgfont_gcm
PPU_LDLIBDIR += -L../../lib
PPU_LIBS += $(GCM_UTIL)/gcmutil.a

PPU_OPTIMIZE_LV = -O2
PPU_INCDIRS += -I../../include -I../../feat -I$(GCM_UTIL)

GCC_PPU_CXXFLAGS += -fno-exceptions -fno-rtti

# scetool: github.com/naehrwert/scetool
SCETOOL_FLAGS := -0 SELF
SCETOOL_FLAGS += -1 TRUE
SCETOOL_FLAGS += -s FALSE
SCETOOL_FLAGS += -3 1010000001000003
SCETOOL_FLAGS += -4 01000002
SCETOOL_FLAGS += -5 NPDRM
SCETOOL_FLAGS += -A 0001000000000000
SCETOOL_FLAGS += -6 0003004000000000
SCETOOL_FLAGS += -8 4000000000000000000000000000000000000000000000000000000000000002
SCETOOL_FLAGS += -9 00000000000000000000000000000000000000000000007B0000000100000000
SCETOOL_FLAGS += -b FREE
SCETOOL_FLAGS += -c EXEC
SCETOOL_FLAGS += -g EBOOT.BIN
SCETOOL_FLAGS += -j TRUE

MAKE_FSELF = make_fself.exe $(1) $(2)
MAKE_SELF_NPDRM = scetool $(SCETOOL_FLAGS) -f $(3) -2 $(4) -e $(1) $(2)

include $(CELL_MK_DIR)/sdk.target.mk

EBOOT.BIN: $(TARGET)
ifeq ($(UNSIGNED), 1)
	$(call MAKE_FSELF,$<,$@)
else
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)
endif
