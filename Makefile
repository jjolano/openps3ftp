# OpenPS3FTP Makefile

BUILDDIR ?= build

# SDK: LINUX, PSL1GHT
SDK ?= LINUX
SDK_MK = $(shell echo $(SDK) | tr '[:upper:]' '[:lower:]')

ifeq ($(SDK), PSL1GHT)
TARGET		:= openps3ftp
LIBNAME		:= lib$(TARGET)_psl1ght.a
endif

ifeq ($(SDK), LINUX)
TARGET		:= ftp
endif

ELFNAME		?= $(TARGET).elf
LIBNAME		?= lib$(TARGET).a

# Define pkg file and application information
ifeq ($(SDK), PSL1GHT)
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337
endif

ifneq ($(SDK), LINUX)
include $(PSL1GHT)/ppu_rules

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

# Make rules
.PHONY: all clean distclean prx prxclean ps3dist ps3clean ps3distclean dist pkg elf lib PARAM.SFO $(ELF_LOC) $(LIB_LOC)

all: elf

clean: 
ifneq ($(SDK),LINUX)
	rm -rf $(APPID)/ $(BUILDDIR)/
	rm -f $(APPID).pkg EBOOT.BIN PARAM.SFO
endif
	$(MAKE) -C bin -f $(ELF_MK) clean
	$(MAKE) -C lib SDK=$(SDK_MK) clean

ifneq ($(SDK),LINUX)
distclean: clean
	rm -f $(TARGET).zip

dist: distclean $(TARGET).zip
endif

prx:
	$(MAKE) -C lib SDK=cell
	$(MAKE) -C bin -f Makefile.cell.prx.mk

prxclean:
	$(MAKE) -C lib SDK=cell clean
	$(MAKE) -C bin -f Makefile.cell.prx.mk clean
	rm -f bin/FTPD_verlog.txt bin/libopenps3ftp_prx.a

ps3:
	$(MAKE) SDK=PSL1GHT

ps3clean:
	$(MAKE) clean SDK=PSL1GHT

ps3dist:
	$(MAKE) dist SDK=PSL1GHT

ps3distclean:
	$(MAKE) distclean SDK=PSL1GHT

ifneq ($(SDK),LINUX)
pkg: $(APPID).pkg
endif

lib: $(LIB_LOC)

elf: lib $(ELF_LOC)

ifneq ($(SDK),LINUX)
$(TARGET).zip: $(APPID).pkg
	mkdir -p $(BUILDDIR)/cex $(BUILDDIR)/rex
	cp $< $(BUILDDIR)/cex/
	cp $< $(BUILDDIR)/rex/
	-$(PACKAGE_FINALIZE) $(BUILDDIR)/cex/$<
	zip -r9ql $(CURDIR)/$@ build readme.txt changelog.txt

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
	$(MAKE) -C lib SDK=$(SDK_MK)
