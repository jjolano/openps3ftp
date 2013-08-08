#
# OpenPS3FTP Makefile
#

# Define pkg file and application information
#
TITLE		:= OpenPS3FTP
TITLE_DEX	:= OpenPS3FTP (DEX)

APPID		:= NPXS91337
APPID_DEX	:= TEST91337

SFOXML		:= $(CURDIR)/app/sfo.xml
ICON0		:= $(CURDIR)/app/ICON0.PNG

LICENSEID	:= AF43D15A870659F4

CONTENTID	:= UP0001-$(APPID)_00-$(LICENSEID)
CONTENTID_DEX	:= UP0001-$(APPID_DEX)_00-$(LICENSEID)

# Setup commands
#

ifneq ($(shell command -v scetool),) 
	SELF_TOOL = $(VERB) scetool
else 
	# userdefined
	SELF_TOOL = $(VERB) ../scetool/scetool

	# fallback
	ifeq ($(wildcard $(SELF_TOOL)),)
		SELF_TOOL = $(VERB) $(SELF_NPDRM)
	endif
endif

MAKE_FSELF = $(VERB) $(FSELF) $(1) $(2)
MAKE_PKG = $(VERB) $(PKG) --contentid $(3) $(1) $(2)
MAKE_SFO = $(VERB) $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)
SIGN_PKG_NPDRM = $(VERB) $(PACKAGE_FINALIZE) $(1)

ifneq ($(SELF_TOOL),$(SELF_NPDRM))
	SIGN_SELF_NPDRM = $(SELF_TOOL) --sce-type=SELF --compress-data=TRUE --skip-sections=TRUE --self-auth-id=1010000001000003 --self-vendor-id=01000002 --self-type=NPDRM --self-app-version=0001000000000000 --self-fw-version=0001008000000000 --self-add-shdrs=TRUE --np-license-type=FREE --np-app-type=EXEC --np-real-fname=EBOOT.BIN --np-content-id=$(3) --key-revision=$(4) --encrypt $(1) $(2)
else
	SIGN_SELF_NPDRM = $(SELF_TOOL) $(1) $(2) (3)
endif

ifndef VERBOSE
	SILENCE := > /dev/null
	SIGN_SELF_NPDRM += $(SILENCE)
	SIGN_PKG_NPDRM += $(SILENCE)
	MAKE_FSELF += $(SILENCE)
	MAKE_PKG += $(SILENCE)
	MAKE_SFO += $(SILENCE)
endif

# Check PSL1GHT environment variable
#
ifeq ($(strip $(PSL1GHT)),)
	$(error PSL1GHT is not installed/configured on this system.)
endif

include $(PSL1GHT)/ppu_rules

# Libraries
#
LIBPATHS	:= -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lNoRSX -lnet -lnetctl -lio -lsysfs -lfreetype -lpixman-1 -lz -lrt -llv2 -lrsx -lgcm_sys -lsysutil -lsysmodule

# Includes
#
INCLUDE		:= -I$(CURDIR)/include -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
#
CFILES		:= $(foreach dir,$(CURDIR),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES	:= $(foreach dir,$(CURDIR),$(notdir $(wildcard $(dir)/*.cpp)))

OFILES		:= $(CPPFILES:.cpp=.o) $(CFILES:.c=.o)

# Define compilation options
#
CFLAGS		= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	= $(CFLAGS)
LDFLAGS		= $(MACHDEP)

ifeq ($(strip $(CPPFILES)),)
	LD	:= $(CC)
else
	LD	:= $(CXX)
endif

# Make rules
#
.PHONY		: all dist clean

all		: $(TARGET).elf

clean		: 
		  $(VERB) echo clean ...
		  $(VERB) rm -f $(OFILES) $(TARGET).elf $(TARGET).zip
		  $(VERB) rm -rf $(BUILDDIR)

dist		: pkg-dex pkg-cex $(TARGET).zip

dist2		: pkg-ncex dist

eboot-us	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call MAKE_FSELF,$<,$(BUILDDIR)/$@/EBOOT.BIN)

eboot-os	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call SIGN_SELF_NPDRM,$<,$(BUILDDIR)/$@/EBOOT.BIN,$(CONTENTID),01)

eboot-ns	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call SIGN_SELF_NPDRM,$<,$(BUILDDIR)/$@/EBOOT.BIN,$(CONTENTID),10)

pkg-dex		: eboot-us
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/x$@/USRDIR
		  $(VERB) ln -fs $(ICON0) $(BUILDDIR)/x$@/
		  $(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE_DEX),$(APPID_DEX))
		  $(VERB) ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID_DEX).pkg,$(CONTENTID_DEX))

pkg-cex		: eboot-os
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/x$@/USRDIR
		  $(VERB) ln -fs $(ICON0) $(BUILDDIR)/x$@/
		  $(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE),$(APPID))
		  $(VERB) ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID).pkg,$(CONTENTID))
		  $(call SIGN_PKG_NPDRM,$(BUILDDIR)/$@/$(CONTENTID).pkg)

pkg-ncex	: eboot-ns
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/x$@/USRDIR
		  $(VERB) ln -fs $(ICON0) $(BUILDDIR)/x$@/
		  $(call MAKE_SFO,$(SFOXML),$(BUILDDIR)/x$@/PARAM.SFO,$(TITLE),$(APPID))
		  $(VERB) ln -fs $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/x$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(call MAKE_PKG,$(BUILDDIR)/x$@/,$(BUILDDIR)/$@/$(CONTENTID).pkg,$(CONTENTID))
		  $(call SIGN_PKG_NPDRM,$(BUILDDIR)/$@/$(CONTENTID).pkg)

$(TARGET).elf	: $(OFILES)
		  $(VERB) echo linking $@ ...
		  $(VERB) $(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
		  $(VERB) $(STRIP) $@
		  $(VERB) $(SPRX) $@

%.o		: %.cpp
		  $(VERB) echo compiling $< ...
		  $(VERB) $(CXX) $(CXXFLAGS) -c $< -o $@ $(ERROR_FILTER)

%.zip		:
		  $(VERB) echo creating $@ ...
		  $(VERB) zip -lr9 $@ README.txt ChangeLog.txt $(notdir $(BUILDDIR))/pkg-*/ $(SILENCE)
