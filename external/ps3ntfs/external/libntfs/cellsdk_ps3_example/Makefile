CELL_SDK ?= /c/cell
CELL_MK_DIR = $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk

PPU_SRCS = main.c
#PPU_SRCS = main_alt.c
PPU_TARGET = libntfs_sample.elf

PPU_INCDIRS = -I../include
PPU_LIBS	= ../libntfs.a
PPU_LDLIBS 	+= -lfs_stub -lsysmodule_stub

PPU_CFLAGS += -O2 -std=gnu99

ifeq ($(shell uname), Linux)
	PACKAGER = wine psn_package_npdrm$(EXE_SUFFIX)
	MODELF = wine modElf
else
	PACKAGER = psn_package_npdrm$(EXE_SUFFIX)
	MODELF = modELF$(EXE_SUFFIX)
endif

PKG_CONFIG = package.conf
CONTENT_ID = IV0002-NTFS00001_00-SAMPLE0000000001
PKG_TARGET = $(CONTENT_ID).pkg
MAKE_SELF = scetool$(EXE_SUFFIX) -v --sce-type=SELF --compress-data=FALSE --skip-sections=FALSE --key-revision=04 --self-ctrl-flags=4000000000000000000000000000000000000000000000000000000000000002 --self-auth-id=1010000001000003 --self-app-version=0001000000000000 --self-add-shdrs=TRUE --self-vendor-id=01000002 --self-type=NPDRM --self-fw-version=0003004000000000 --np-license-type=FREE --np-content-id=$(CONTENT_ID) --np-app-type=EXEC --np-real-fname=EBOOT.BIN --encrypt 

USER_DIR = $(OBJS_DIR)/USRDIR

CONTENTS = $(OBJS_DIR)/PARAM.SFO $(OBJS_DIR)/ICON0.PNG $(USER_DIR)/EBOOT.BIN

EXTRA_TARGET = $(PKG_TARGET)
CLEANFILES = $(PKG_TARGET) $(CONTENTS)

$(PKG_TARGET): $(PKG_CONFIG) $(CONTENTS)
	@echo \# generate sample npdrm package in here.
	$(PACKAGER) $(PKG_CONFIG) $(OBJS_DIR)

$(OBJS_DIR)/PARAM.SFO: PARAM.SFO
	@mkdir -p $(@D)
	@$(CP) $< $@

$(OBJS_DIR)/ICON0.PNG: ICON0.PNG
	@mkdir -p $(@D)
	@$(CP) $< $@
	
$(USER_DIR)/EBOOT.BIN: $(PPU_TARGET)
	@echo \# make EBOOT.BIN in $(USER_DIR)/.
	@mkdir -p $(@D)
	$(MODELF) $<
	$(MAKE_SELF) $< $@

include $(CELL_MK_DIR)/sdk.target.mk
