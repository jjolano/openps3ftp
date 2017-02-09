# OpenPS3FTP Makefile

BUILDDIR ?= build

# SDK: LINUX, PSL1GHT, CELL
SDK ?= LINUX
SDK_MK = $(shell echo $(SDK) | tr '[:upper:]' '[:lower:]')

ifeq ($(SDK),CELL)
TARGET		:= cellps3ftp
endif

ifeq ($(SDK),PSL1GHT)
include $(PSL1GHT)/ppu_rules
TARGET		:= openps3ftp
endif

ifeq ($(SDK),LINUX)
TARGET		:= ftpxx
endif

ELFNAME		:= $(TARGET).elf
LIBNAME		:= lib$(TARGET).a

# Define pkg file and application information
ifeq ($(SDK),PSL1GHT)
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337
endif

ifeq ($(SDK),CELL)
TITLE		:= CellPS3FTP
APPID		:= NPXS01337
endif

ifneq ($(SDK),LINUX)
CONTENTID	:= UP0001-$(APPID)_00-0000000000000000
SFOXML		:= $(CURDIR)/pkg/sfo.xml
ICON0		:= $(CURDIR)/pkg/ICON0.PNG

# Setup commands
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

MAKE_SELF_NPDRM = scetool $(SCETOOL_FLAGS) -f $(3) -2 $(4) -e $(1) $(2)
MAKE_PKG = $(PKG) --contentid $(3) $(1) $(2)
MAKE_FSELF = $(FSELF) $(1) $(2)
MAKE_SFO = $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)
endif

ELF_LOC	:= ./bin/$(ELFNAME)
LIB_LOC	:= ./lib/$(LIBNAME)

ELF_MK	:= Makefile.$(SDK_MK).elf.mk
LIB_MK	:= Makefile.$(SDK_MK).lib.mk

# Make rules
.PHONY: all clean distclean sdkall sdkdist sdkclean sdkdistclean dist pkg elf lib install

all: elf

clean: 
ifneq ($(SDK),LINUX)
	rm -rf $(APPID)/ $(BUILDDIR)/
	rm -f $(APPID).pkg EBOOT.BIN PARAM.SFO
endif
	$(MAKE) -C bin -f $(ELF_MK) clean
	$(MAKE) -C lib -f $(LIB_MK) clean

ifneq ($(SDK),LINUX)
distclean: clean
	rm -f $(TARGET).zip

dist: distclean $(TARGET).zip
endif

sdkall: sdkclean
	$(MAKE) all SDK=LINUX
	$(MAKE) all SDK=PSL1GHT
	$(MAKE) all SDK=CELL

sdkclean:
	$(MAKE) clean SDK=LINUX
	$(MAKE) clean SDK=PSL1GHT
	$(MAKE) clean SDK=CELL

sdkdist:
	$(MAKE) dist SDK=PSL1GHT
	$(MAKE) dist SDK=CELL

sdkdistclean:
	$(MAKE) clean SDK=LINUX
	$(MAKE) distclean SDK=PSL1GHT
	$(MAKE) distclean SDK=CELL

ifneq ($(SDK),LINUX)
pkg: $(APPID).pkg
endif

lib: $(LIB_LOC)

elf: lib $(ELF_LOC)

install: lib
	$(MAKE) -C lib -f $(LIB_MK) install

ifneq ($(SDK),LINUX)
$(TARGET).zip: all
	mkdir -p $(BUILDDIR)/cex $(BUILDDIR)/rex
	cp $(APPID).pkg $(BUILDDIR)/cex/
	cp $(APPID).pkg $(BUILDDIR)/rex/
	-$(PACKAGE_FINALIZE) $(BUILDDIR)/cex/$(APPID).pkg
	zip -r9ql $(CURDIR)/$(TARGET).zip build readme.txt changelog.txt

EBOOT.BIN: elf
	$(call MAKE_SELF_NPDRM,$(ELF_LOC),$@,$(CONTENTID),04)

$(APPID).pkg: EBOOT.BIN PARAM.SFO $(ICON0)
	mkdir -p $(APPID)/USRDIR
	cp $(ICON0) $(APPID)/
	cp PARAM.SFO $(APPID)/
	cp EBOOT.BIN $(APPID)/USRDIR/
	$(call MAKE_PKG,$(APPID)/,$@,$(CONTENTID))

PARAM.SFO: $(SFOXML)
	$(call MAKE_SFO,$<,$@,$(TITLE),$(APPID))
endif

$(ELF_LOC):
	$(MAKE) -C bin -f $(ELF_MK)

$(LIB_LOC):
	$(MAKE) -C lib -f $(LIB_MK)
