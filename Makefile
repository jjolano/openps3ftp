# OpenPS3FTP Makefile
# Check PSL1GHT environment variable
ifeq ($(strip $(PSL1GHT)),)
	$(error PSL1GHT is not installed/configured on this system.)
endif

include $(PSL1GHT)/ppu_rules

TARGET		:= openps3ftp
SIGN_SELF	:= 1

# Define pkg file and application information
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337

# Setup commands
# scetool: github.com/naehrwert/scetool

SELF_TOOL = scetool

MAKE_SELF_NPDRM = $(SELF_TOOL) -0 SELF -1 TRUE -s FALSE -3 1010000001000003 -4 01000002 -5 NPDRM -A 0001000000000000 -6 0003004000000000 -b FREE -c EXEC -g EBOOT.BIN -f $(3) -2 $(4) -e $(1) $(2)
MAKE_PKG = $(PKG) --contentid $(3) $(1) $(2)
MAKE_FSELF = $(FSELF) $(1) $(2)
MAKE_SFO = $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)

# Libraries
LIBPATHS	:= -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lnetctl -lnet -lNoRSX -lgcm_sys -lrsx -lfreetype -lsysutil -lrt -llv2 -lsysmodule -lm -lz -lsysfs

# Includes
INCLUDE		:= -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRC			:= $(wildcard *.cpp)
OFILES		:= $(SRC:.cpp=.o)

# Define compilation options
CFLAGS		= -O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= $(MACHDEP) -s

# Make rules
.PHONY: all clean dist

all: $(TARGET).elf

clean: 
	rm -f $(OFILES)
	rm -f $(TARGET).elf
	rm -f $(CONTENTID).pkg
	rm -f EBOOT.BIN
	rm -f PARAM.SFO
	rm -rf $(CONTENTID)

dist: clean $(CONTENTID).pkg

EBOOT.BIN: $(TARGET).elf
ifeq ($(SIGN_SELF), 1)
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)
else
	$(call MAKE_FSELF,$<,$@)
endif

PARAM.SFO: sfo.xml
	$(call MAKE_SFO,$<,$@,$(TITLE),$(APPID))

$(CONTENTID).pkg: EBOOT.BIN PARAM.SFO ICON0.PNG
	mkdir -p $(CONTENTID)/USRDIR
	cp ICON0.PNG $(CONTENTID)/
	cp PARAM.SFO $(CONTENTID)/
	cp EBOOT.BIN $(CONTENTID)/USRDIR/
	$(call MAKE_PKG,$(CONTENTID)/,$@,$(CONTENTID))

$(TARGET).elf: $(OFILES)
	$(CXX) $^ $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
	$(SPRX) $@

%.o: %.cpp
	$(CXX) $(CFLAGS) -c $< -o $@
