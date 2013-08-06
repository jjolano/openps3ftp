#
# OpenPS3FTP Makefile
#

# Define pkg file and application information
#
APPID		:= NPXS91337
APPID_DEX	:= TEST91337

SFOXML		:= $(CURDIR)/app/sfo_cex.xml
SFOXML_DEX	:= $(CURDIR)/app/sfo_dex.xml

ICON0		:= $(CURDIR)/app/ICON0.PNG

LICENSEID	:= AF43D15A870659F4

CONTENTID	:= UP0001-$(APPID)_00-$(LICENSEID)
CONTENTID_DEX	:= UP0001-$(APPID_DEX)_00-$(LICENSEID)

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
		  $(VERB) rm -f $(OFILES) $(TARGET).elf $(TARGET).zip
		  $(VERB) rm -rf $(BUILDDIR)

dist		: $(TARGET).zip

eboot-debug	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [devkit] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(VERB) $(FSELF) $< $(BUILDDIR)/$@/EBOOT.BIN

eboot-retail	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [npdrm-retail] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(VERB) $(SELF_NPDRM) $< $(BUILDDIR)/$@/EBOOT.BIN $(CONTENTID) > /dev/null
		  $(VERB) echo "** Note that this build is for firmware 3.55. **"
		  $(VERB) echo "** You will need to re-sign the EBOOT and pkg **"
		  $(VERB) echo "** files if on a FW version higher than 3.55. **"

pkg-debug	: eboot-debug
		  $(VERB) echo creating pkg [devkit] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/$@/
		  $(VERB) $(SFO) -f $(SFOXML_DEX) $(BUILDDIR)/$@/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/debug
		  $(VERB) $(PKG) -c $(CONTENTID_DEX) $(BUILDDIR)/$@/ $(BUILDDIR)/pkg/debug/$(CONTENTID_DEX).pkg > /dev/null

pkg-retail	: eboot-retail
		  $(VERB) echo creating pkg [npdrm-retail] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/$@/
		  $(VERB) $(SFO) -f $(SFOXML_DEX) $(BUILDDIR)/$@/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/retail
		  $(VERB) $(PKG) -c $(CONTENTID) $(BUILDDIR)/$@/ $(BUILDDIR)/pkg/retail/$(CONTENTID).pkg > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $(BUILDDIR)/pkg/retail/$(CONTENTID).pkg

$(TARGET).elf	: $(OFILES)
		  $(VERB) echo creating $@ ...
		  $(VERB) $(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
		  $(VERB) $(STRIP) $@
		  $(VERB) $(SPRX) $@

%.zip		: pkg-debug pkg-retail
		  $(VERB) echo creating $@ ...
		  $(VERB) zip -lr9 $@ README.txt ChangeLog.txt $(notdir $(BUILDDIR))/pkg/ > /dev/null
