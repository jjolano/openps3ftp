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
		  $(VERB) rm -f $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(DEPENDS)
		  $(VERB) rm -f $(TARGET).elf $(TARGET).zip
		  $(VERB) rm -rf $(BUILDDIR)

dist		: $(TARGET).zip

eboot-debug	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [devkit] ...
		  $(VERB) mkdir -p $(BUILDDIR)/eboot-debug
		  $(VERB) $(FSELF) $< $(BUILDDIR)/eboot-debug/EBOOT.BIN

eboot-retail	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [npdrm-retail] ...
		  $(VERB) mkdir -p $(BUILDDIR)/eboot-retail
		  $(VERB) $(SELF_NPDRM) $< $(BUILDDIR)/eboot-retail/EBOOT.BIN $(CONTENTID) > /dev/null

pkg-debug	: eboot-debug
		  $(VERB) echo creating pkg [devkit] ...
		  $(VERB) mkdir -p $(BUILDDIR)/pkg-debug/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/pkg-debug/
		  $(VERB) $(SFO) -f $(SFOXML_DEX) $(BUILDDIR)/pkg-debug/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/eboot-debug/EBOOT.BIN $(BUILDDIR)/pkg-debug/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/debug
		  $(VERB) $(PKG) -c $(CONTENTID_DEX) $(BUILDDIR)/pkg-debug/ $(BUILDDIR)/pkg/debug/$(CONTENTID_DEX).pkg > /dev/null

pkg-retail	: eboot-retail
		  $(VERB) echo creating pkg [npdrm-retail] ...
		  $(VERB) mkdir -p $(BUILDDIR)/pkg-retail/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/pkg-retail/
		  $(VERB) $(SFO) -f $(SFOXML_DEX) $(BUILDDIR)/pkg-retail/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/eboot-retail/EBOOT.BIN $(BUILDDIR)/pkg-retail/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/retail
		  $(VERB) $(PKG) -c $(CONTENTID) $(BUILDDIR)/pkg-retail/ $(BUILDDIR)/pkg/retail/$(CONTENTID).pkg > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $(BUILDDIR)/pkg/retail/$(CONTENTID).pkg

$(TARGET).elf	: $(OFILES)
		  $(VERB) echo creating $@ ...
		  $(VERB) $(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
		  $(VERB) $(STRIP) $@
		  $(VERB) $(SPRX) $@

%.zip		: pkg-debug pkg-retail
		  $(VERB) echo creating $@ ...
		  $(VERB) zip -lr9 $@ README.txt ChangeLog.txt $(notdir $(BUILDDIR))/pkg/ > /dev/null
