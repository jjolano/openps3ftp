# OpenPS3FTP Makefile
# Check PSL1GHT environment variable
ifeq ($(strip $(PSL1GHT)),)
	$(error PSL1GHT is not installed/configured on this system.)
endif

include $(PSL1GHT)/ppu_rules

TARGET		:= openps3ftp
LIBNAME		:= lib$(TARGET)

# Define pkg file and application information
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337
SFOXML		:= $(CURDIR)/pkg-meta/sfo.xml
ICON0		:= $(CURDIR)/pkg-meta/ICON0.PNG

# Setup commands
# scetool: github.com/naehrwert/scetool
MAKE_SELF_NPDRM = scetool -0 SELF -1 TRUE -s FALSE -3 1010000001000003 -4 01000002 -5 NPDRM -A 0001000000000000 -6 0003004000000000 -b FREE -c EXEC -g EBOOT.BIN -f $(3) -2 $(4) -e $(1) $(2) -j TRUE
MAKE_PKG = $(PKG) --contentid $(3) $(1) $(2)
MAKE_FSELF = $(FSELF) $(1) $(2)
MAKE_SFO = $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)

# Libraries
LIBPATHS	:= -L. -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -l$(TARGET) -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2 -lm -lz

# Includes
INCLUDE		:= -I$(CURDIR)/ftp -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRC			:= $(wildcard *.cpp)
OBJ			:= $(SRC:.cpp=.o)

# Define compilation options
CXXFLAGS	= -Os -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= $(MACHDEP) -s

# Make rules
.PHONY: all clean dist pkg lib install

all: $(TARGET).elf

clean: 
	rm -f $(OBJ) $(LIBNAME).a $(TARGET).zip $(TARGET).self $(TARGET).elf $(CONTENTID).pkg EBOOT.BIN PARAM.SFO
	rm -rf $(CONTENTID) $(BUILDDIR)

dist: clean $(TARGET).zip

pkg: $(CONTENTID).pkg

lib: $(LIBNAME).a

install: lib
	cp -fr $(CURDIR)/ftp $(PORTLIBS)/include/
	cp -f $(LIBNAME).a $(PORTLIBS)/lib/

$(LIBNAME).a: $(OBJ:main.o=)
	$(AR) -rc $@ $^

$(TARGET).zip: $(CONTENTID).pkg
	mkdir -p $(BUILDDIR)/npdrm $(BUILDDIR)/rex
	cp $< $(BUILDDIR)/npdrm/
	cp $< $(BUILDDIR)/rex/
	-$(PACKAGE_FINALIZE) $(BUILDDIR)/npdrm/$<
	zip -r9ql $(CURDIR)/$(TARGET).zip build readme.txt changelog.txt

EBOOT.BIN: $(TARGET).elf
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)

$(CONTENTID).pkg: EBOOT.BIN PARAM.SFO $(ICON0)
	mkdir -p $(CONTENTID)/USRDIR
	cp $(ICON0) $(CONTENTID)/
	cp PARAM.SFO $(CONTENTID)/
	cp EBOOT.BIN $(CONTENTID)/USRDIR/
	$(call MAKE_PKG,$(CONTENTID)/,$@,$(CONTENTID))

PARAM.SFO: $(SFOXML)
	$(call MAKE_SFO,$<,$@,$(TITLE),$(APPID))

$(TARGET).self: $(TARGET).elf
	$(call MAKE_FSELF,$<,$@)

$(TARGET).elf: main.o $(LIBNAME).a
	$(CXX) $< $(LDFLAGS) $(LIBPATHS) $(LIBS) -o $@
	$(SPRX) $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
