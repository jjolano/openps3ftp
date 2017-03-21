# OpenPS3FTP Makefile
BUILDDIR ?= build

# SDK: linux, psl1ght
SDK ?= linux

ifeq ($(SDK), psl1ght)
include $(PSL1GHT)/ppu_rules

TITLE		:= OpenPS3FTP
APPID		:= NPXS91337

CONTENTID	:= UP0001-$(APPID)_00-0000000000000000
SFOXML		:= $(CURDIR)/pkg/sfo.xml
ICON0		:= $(CURDIR)/pkg/ICON0.PNG

TARGET		:= openps3ftp
LIBNAME		:= lib$(TARGET)_psl1ght.a

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

TARGET		?= ftp
ELFNAME		?= $(TARGET).elf
LIBNAME		?= lib$(TARGET).a

ELF_LOC	:= ./bin/$(SDK)/$(ELFNAME)
LIB_LOC	:= ./lib/$(LIBNAME)

# Make rules
.PHONY: all clean distclean prx prxclean ps3dist ps3clean ps3distclean dist pkg elf lib PARAM.SFO $(ELF_LOC) $(LIB_LOC)

all: elf

clean: 
ifneq ($(SDK), linux)
	rm -rf $(APPID)/ $(BUILDDIR)/
	rm -f $(APPID).pkg EBOOT.BIN PARAM.SFO
	rm -f $(TARGET).zip
endif
	$(MAKE) -C bin/$(SDK) clean
	$(MAKE) -C lib SDK=$(SDK) clean LIB_EXTERNAL=1
	$(MAKE) -C lib SDK=$(SDK) clean

distclean: clean prxclean ps3clean

prx:
	$(MAKE) -C external/ps3ntfs
	$(MAKE) -C lib SDK=cell
	$(MAKE) -C bin/prx

prxclean:
	$(MAKE) -C external/ps3ntfs clean
	$(MAKE) -C lib SDK=cell clean
	$(MAKE) -C bin/prx clean

ps3:
	$(MAKE) SDK=psl1ght

ps3clean:
	$(MAKE) SDK=psl1ght clean

ps3dist:
	$(MAKE) SDK=psl1ght dist

lib: $(LIB_LOC)

elf: lib $(ELF_LOC)

$(ELF_LOC):
	$(MAKE) -C bin/$(SDK)

$(LIB_LOC):
ifneq ($(SDK), psl1ght)
	$(MAKE) -C lib SDK=$(SDK) all
else
	$(MAKE) -C lib SDK=$(SDK) all LIB_EXTERNAL=1
endif

ifneq ($(SDK), linux)
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

pkg: $(APPID).pkg

dist: distclean $(TARGET).zip
endif
