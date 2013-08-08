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

# Use this if available
#
ifneq ($(shell command -v scetool),) 
	SCETOOL = scetool
else 
	SCETOOL = ../scetool/scetool
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
		  $(VERB) rm -f $(OFILES) $(TARGET).elf $(TARGET).zip
		  $(VERB) rm -rf $(BUILDDIR)

dist		: $(TARGET).zip

dist2		: pkg-cex-new dist

eboot-dex	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(VERB) $(FSELF) $< $(BUILDDIR)/$@/EBOOT.BIN

eboot-cex-old	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(VERB) if [ -a $(SCETOOL) ]; then \
		  echo "signing EBOOT.BIN using scetool ..."; \
		  $(SCETOOL) --sce-type=SELF --compress-data=TRUE --skip-sections=TRUE --key-revision=01 --self-auth-id=1010000001000003 --self-vendor-id=01000002 --self-type=NPDRM --self-app-version=0001000000000000 --self-fw-version=0001008000000000 --self-add-shdrs=TRUE --np-license-type=FREE --np-app-type=EXEC --np-content-id=$(CONTENTID) --np-real-fname=EBOOT.BIN --encrypt $< $(notdir $(BUILDDIR))/$@/EBOOT.BIN > /dev/null; \
		  else \
		  echo "signing EBOOT.BIN using $(SELF_NPDRM) ..."; \
		  $(SELF_NPDRM) $< $(BUILDDIR)/$@/EBOOT.BIN $(CONTENTID) > /dev/null; \
		  fi;

eboot-cex-new	: $(TARGET).elf
		  $(VERB) echo creating EBOOT.BIN [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@
		  $(VERB) if [ -a $(SCETOOL) ]; then \
		  $(SCETOOL) --sce-type=SELF --compress-data=TRUE --skip-sections=TRUE --key-revision=10 --self-auth-id=1010000001000003 --self-vendor-id=01000002 --self-type=NPDRM --self-app-version=0001000000000000 --self-fw-version=0001008000000000 --self-add-shdrs=TRUE --np-license-type=FREE --np-app-type=EXEC --np-content-id=$(CONTENTID) --np-real-fname=EBOOT.BIN --encrypt $< $(notdir $(BUILDDIR))/$@/EBOOT.BIN > /dev/null; \
		  else \
		  echo "error: no scetool"; \
		  exit 1; \
		  fi;

pkg-dex		: eboot-dex
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/$@/
		  $(VERB) $(SFO) -f $(SFOXML_DEX) $(BUILDDIR)/$@/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/dex
		  $(VERB) $(PKG) -c $(CONTENTID_DEX) $(BUILDDIR)/$@/ $(BUILDDIR)/pkg/dex/$(CONTENTID_DEX).pkg > /dev/null

pkg-cex-old	: eboot-cex-old
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/$@/
		  $(VERB) $(SFO) -f $(SFOXML) $(BUILDDIR)/$@/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/cex-old
		  $(VERB) $(PKG) -c $(CONTENTID) $(BUILDDIR)/$@/ $(BUILDDIR)/pkg/cex-old/$(CONTENTID).pkg > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $(BUILDDIR)/pkg/cex-old/$(CONTENTID).pkg

pkg-cex-new	: eboot-cex-new
		  $(VERB) echo creating pkg [$@] ...
		  $(VERB) mkdir -p $(BUILDDIR)/$@/USRDIR
		  $(VERB) cp -f $(ICON0) $(BUILDDIR)/$@/
		  $(VERB) $(SFO) -f $(SFOXML) $(BUILDDIR)/$@/PARAM.SFO
		  $(VERB) cp -f $(BUILDDIR)/$</EBOOT.BIN $(BUILDDIR)/$@/USRDIR/
		  $(VERB) mkdir -p $(BUILDDIR)/pkg/cex-new
		  $(VERB) $(PKG) -c $(CONTENTID) $(BUILDDIR)/$@/ $(BUILDDIR)/pkg/cex-new/$(CONTENTID).pkg > /dev/null
		  $(VERB) $(PACKAGE_FINALIZE) $(BUILDDIR)/pkg/cex-new/$(CONTENTID).pkg

$(TARGET).elf	: $(OFILES)
		  $(VERB) echo creating $@ ...
		  $(VERB) $(LD) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
		  $(VERB) $(STRIP) $@
		  $(VERB) $(SPRX) $@

%.zip		: pkg-dex pkg-cex-old
		  $(VERB) echo creating $@ ...
		  $(VERB) zip -lr9 $@ README.txt ChangeLog.txt $(notdir $(BUILDDIR))/pkg/ > /dev/null
