# OpenPS3FTP Makefile
# Define pkg file and application information

TITLE		:= OpenPS3FTP
TITLE_DEX	:= OpenPS3FTP (DEX)

APPID		:= NPXS91337
APPID_DEX	:= TEST91337

SFOXML		:= $(CURDIR)/app/sfo.xml
ICON0		:= $(CURDIR)/app/ICON0.PNG

LICENSEID	:= $(shell openssl rand -hex 8 | tr [:lower:] [:upper:])

CONTENTID	:= UP0001-$(APPID)_00-$(LICENSEID)
CONTENTID_DEX	:= UP0001-$(APPID_DEX)_00-$(LICENSEID)

# Setup commands

# scetool: github.com/naehrwert/scetool
SELF_TOOL = scetool

# make_gpkg: github.com/jjolano/make_gpkg
PKG_TOOL = make_gpkg

# make_fself: github.com/jjolano/make_fself
FSELF_TOOL = make_fself

MAKE_SELF_NPDRM = $(SELF_TOOL) --sce-type=SELF --compress-data=TRUE --skip-sections=FALSE --self-auth-id=1010000001000003 --self-vendor-id=01000002 --self-type=NPDRM --self-app-version=0001000000000000 --self-fw-version=0003004000000000 --np-license-type=FREE --np-app-type=EXEC --np-real-fname=EBOOT.BIN --np-content-id=$(3) --key-revision=$(4) --encrypt $(1) $(2)

MAKE_PKG = $(PKG_TOOL) $(3) $(1) $(2)

MAKE_FSELF = $(FSELF_TOOL) $(1) $(2)

MAKE_SFO = $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)

FINALIZE_PKG = $(PACKAGE_FINALIZE) $(1)

# Check PSL1GHT environment variable
ifeq ($(strip $(PSL1GHT)),)
	$(error PSL1GHT is not installed/configured on this system.)
endif

include $(PSL1GHT)/ppu_rules

# Libraries
LIBPATHS	:= -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lNoRSX -lgcm_sys -lrsx -lsysutil -lnetctl -lnet -lio -lsysfs -lrt -llv2 -lfreetype -lz -lsysmodule

# Includes
INCLUDE		:= -I$(CURDIR)/include -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
CPPFILES	:= $(foreach dir,$(CURDIR),$(notdir $(wildcard $(dir)/*.cpp)))
OFILES		:= $(addsuffix .o, $(basename $(CPPFILES)))

# Define compilation options
CFLAGS		= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	= $(CFLAGS)
LDFLAGS		= $(MACHDEP) -s

ifeq ($(strip $(CPPFILES)),)
	LD	:= $(CC)
else
	LD	:= $(CXX)
endif

# Make rules
.PHONY: all dist clean

all: $(TARGET).elf

clean: 
	rm -f $(OFILES) $(TARGET).elf $(TARGET).zip
	rm -rf $(BUILDDIR)

dist: clean pkg-dex pkg-cex pkg-rex $(TARGET).zip

eboot-us: $(TARGET).elf
	@echo creating EBOOT.BIN [$@] ...
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_FSELF,$<,$(BUILDDIR)/$@/EBOOT.BIN)

eboot-os: $(TARGET).elf
	@echo creating EBOOT.BIN [$@] ...
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_SELF_NPDRM,$<,$(BUILDDIR)/$@/EBOOT.BIN,$(CONTENTID),04)

eboot-ns: $(TARGET).elf
	@echo creating EBOOT.BIN [$@] ...
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_SELF_NPDRM,$<,$(BUILDDIR)/$@/EBOOT.BIN,$(CONTENTID),10)

pkg-dex: eboot-us
	@echo creating pkg [$@] ...
	mkdir -p $(BUILDDIR)/x$@/USRDIR
	ln -fs $(ICON0) $(BUILDDIR)/x$@/
	$(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE_DEX),$(APPID_DEX))
	ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID_DEX).pkg,$(CONTENTID_DEX))

pkg-cex: eboot-os
	@echo creating pkg [$@] ...
	mkdir -p $(BUILDDIR)/x$@/USRDIR
	ln -fs $(ICON0) $(BUILDDIR)/x$@/
	$(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE),$(APPID))
	ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID).pkg,$(CONTENTID))
	-$(call FINALIZE_PKG,$(BUILDDIR)/$@/$(CONTENTID).pkg)

pkg-rex: eboot-ns
	@echo creating pkg [$@] ...
	mkdir -p $(BUILDDIR)/x$@/USRDIR
	ln -fs $(ICON0) $(BUILDDIR)/x$@/
	$(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE),$(APPID))
	ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
	mkdir -p $(BUILDDIR)/$@
	$(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID).pkg,$(CONTENTID))

$(TARGET).elf: $(OFILES)
	@echo linking $@ ...
	$(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
	$(SPRX) $@

%.o: %.cpp
	@echo compiling $< ...
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(ERROR_FILTER)

%.zip: 
	@echo creating $@ ...
	zip -lr9 $@ README.txt ChangeLog.txt $(notdir $(BUILDDIR))/pkg-*/
