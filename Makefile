#
# OpenPS3FTP Makefile
#

# Define pkg file and application information
#
APPID		:= OFTP00001
SFOXML		:= $(CURDIR)/app/sfo.xml
ICON0		:= $(CURDIR)/app/ICON0.PNG

PKGFILES	:= 

DISTDIR		:= $(CURDIR)/dist
BUILDDIR	:= $(CURDIR)/build
DEPSDIR		:= $(CURDIR)

# Check PSL1GHT environment variable
#
ifeq ($(strip $(PSL1GHT)),)
$(error PSL1GHT is not installed/configured on this system.)
endif

include $(PSL1GHT)/ppu_rules

# Set ContentID for NPDRM
#
ifneq ($(wildcard $(BUILDDIR)/$(TARGET).elf),)
	CID	:= UP0001-$(APPID)_00-$(shell openssl md5 < $(BUILDDIR)/$(TARGET).elf | head -c16 | tr '[:lower:]' '[:upper:]')
else
	CID	:= $(CONTENTID)
endif

# Libraries
#
LIBPATHS	:= -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lNoRSX -lnet -lnetctl -lio -lsysfs -lm -lfreetype -lpixman-1 -lz -lrt -llv2 -lrsx -lgcm_sys -lsysutil -lsysmodule

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
CFLAGS		= -O3 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	= $(CFLAGS)
LDFLAGS		= $(MACHDEP) -Wl,-Map,$(notdir $@).map

ifeq ($(strip $(CPPFILES)),)
	LD	:= $(CC)
else
	LD	:= $(CXX)
endif

# Dependencies
#
DEPENDS		:= $(OFILES:.o=.d)

# Make rules
#
.PHONY		: all pkg-struct dist dist-debug dist-retail clean distclean

all		: $(TARGET).elf

clean		: 
		  $(VERB) rm -f $(CFILES:.c=.o) $(CPPFILES:.cpp=.o) $(DEPENDS)
		  $(VERB) rm -f $(TARGET).self EBOOT.BIN
		  $(VERB) rm -f $(TARGET).elf $(TARGET).elf.map
		  $(VERB) rm -rf $(BUILDDIR)

distclean	: clean
		  $(VERB) rm -rf $(DISTDIR) $(TARGET).pkg $(TARGET).zip

pkg-struct	: 
		  $(VERB) echo building pkg structure ... 
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/pkg/
		  $(VERB) $(SFO) -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
		  $(VERB) if [ -n "$(PKGFILES)" -a -d "$(PKGFILES)" ]; then cp -rf $(PKGFILES)/* $(BUILDDIR)/pkg/; fi

dist		: $(TARGET).zip

prep-srcdist	: 
		  $(VERB) find . -exec touch -t 197001010000.00 {} \;

# Not sure how debug SELFs should be created.
dist-debug	: $(TARGET).self pkg-struct
		  $(VERB) echo building pkg ... $(CID).pkg
		  $(VERB) [ -d $(DISTDIR)/debug ] || mkdir -p $(DISTDIR)/debug
		  $(VERB) cp -f $(basename $<).self $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
		  $(VERB) $(PKG) --contentid $(CID) $(BUILDDIR)/pkg/ $(DISTDIR)/debug/$(CID).pkg > /dev/null

dist-retail	: EBOOT.BIN pkg-struct
		  $(VERB) echo building pkg ... $(CID).pkg
		  $(VERB) [ -d $(DISTDIR)/retail ] || mkdir -p $(DISTDIR)/retail
		  $(VERB) cp -f $< $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
		  $(VERB) $(PKG) --contentid $(CID) $(BUILDDIR)/pkg/ $(DISTDIR)/retail/$(CID).pkg > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $(DISTDIR)/retail/$(CID).pkg

$(TARGET).elf	: $(OFILES)
		  $(VERB) echo linking ... $(notdir $@)
		  $(VERB) $(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
		  $(VERB) [ -d $(BUILDDIR) ] || mkdir -p $(BUILDDIR)
		  $(VERB) $(STRIP) $@ -o $(BUILDDIR)/$(notdir $@)
		  $(VERB) $(SPRX) $(BUILDDIR)/$(notdir $@)

$(TARGET).self	: $(TARGET).elf
		  $(VERB) echo encrypting [fself] ... $(notdir $@)
		  $(VERB) $(FSELF) $(BUILDDIR)/$(notdir $<) $@

EBOOT.BIN	: $(TARGET).elf
		  $(eval CID := UP0001-$(APPID)_00-$(shell openssl md5 < $(BUILDDIR)/$(notdir $<) | head -c16 | tr '[:lower:]' '[:upper:]'))
		  $(VERB) echo encrypting [geohot-npdrm] ... $(notdir $@)
		  $(VERB) echo contentid: $(CID)
		  $(VERB) $(SELF_NPDRM) $(BUILDDIR)/$(notdir $<) $@ $(CID) > /dev/null

$(TARGET).pkg	: EBOOT.BIN
		  $(VERB) echo building pkg [retail] ... $(notdir $@)
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/pkg/
		  $(VERB) $(SFO) -f $(SFOXML) $(BUILDDIR)/pkg/PARAM.SFO
		  $(VERB) if [ -n "$(PKGFILES)" -a -d "$(PKGFILES)" ]; then cp -rf $(PKGFILES)/* $(BUILDDIR)/pkg/; fi
		  $(VERB) cp -f $< $(BUILDDIR)/pkg/USRDIR/EBOOT.BIN
		  $(VERB) $(PKG) --contentid $(CID) $(BUILDDIR)/pkg/ $@ > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $@

$(TARGET).zip	: distclean dist-retail
		  $(VERB) echo building zip ... $(notdir $@)
		  $(VERB) zip -lr $@ README.txt ChangeLog.txt dist/ > /dev/null

$(TARGET)%.zip	: $(TARGET).zip
		  $(VERB) echo renaming zip ... $(notdir $@)
		  $(VERB) mv -f $< $@

-include $(DEPENDS)
