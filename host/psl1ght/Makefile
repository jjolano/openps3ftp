# OpenPS3FTP Makefile
SSFTP ?= ../../external/libssftp

include $(PSL1GHT)/ppu_rules

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

APPNAME		:= openps3ftp
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337

CONTENTID	:= UP0001-$(APPID)_00-0000000000000000
SFOXML		:= $(CURDIR)/pkg/sfo.xml
ICON0		:= $(CURDIR)/pkg/ICON0.PNG
TARGET		:= $(APPNAME).elf

# Libraries
LIBPATHS	:= -L$(SSFTP) -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lssftp_psl1ght -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2 -lm -lz

# Includes
INCLUDE		:= -I. -I$(SSFTP) -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= main.cpp
OBJS		= $(SRCS:.cpp=.o)

# Define compilation options
DEFINES		= -D__PSL1GHT__
CXXFLAGS	= -ffunction-sections -fdata-sections -O2 -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) $(DEFINES)
CXXFLAGS	+= -D_FILE_OFFSET_BITS=64
LDFLAGS		= -Wl,--gc-sections -s $(MACHDEP) $(LIBPATHS) $(LIBS)

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS)

distclean: clean
	rm -f EBOOT.BIN PARAM.SFO $(APPNAME).pkg $(CONTENTID).pkg
	rm -rf $(APPID)

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(SPRX) $@

EBOOT.BIN: $(TARGET)
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)

PARAM.SFO: $(SFOXML)
	$(call MAKE_SFO,$<,$@,$(TITLE),$(APPID))

%.pkg: EBOOT.BIN PARAM.SFO $(ICON0)
	mkdir -p $(APPID)/USRDIR
	cp $(ICON0) $(APPID)/
	cp PARAM.SFO $(APPID)/
	cp EBOOT.BIN $(APPID)/USRDIR/
	$(call MAKE_PKG,$(APPID)/,$@,$(CONTENTID))

pkg: $(APPNAME).pkg

rpkg: $(CONTENTID).pkg
	-$(PACKAGE_FINALIZE) $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
