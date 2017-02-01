# OpenPS3FTP Makefile
TARGET		:= openps3ftp
TARGET_CELL	:= cellps3ftp

LIBNAME		:= lib$(TARGET)
CELLLIB		:= lib$(TARGET_CELL)

OPTS		:= -D_USE_SYSFS_ -D_USE_FASTPOLL_ -D_USE_IOBUFFERS_

include $(PSL1GHT)/ppu_rules

# Define pkg file and application information
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337
CONTENTID	:= UP0001-NPXS91337_00-0000000000000000
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
LIBS		:= -l$(TARGET) -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lsysfs -llv2 -lrt

# Includes
INCLUDE		:= -I. -I$(CURDIR)/ftp -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRC			:= $(wildcard *.cpp)
OBJ			:= $(SRC:.cpp=.o)

# Define compilation options
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= -s $(MACHDEP) $(LIBPATHS) $(LIBS)

# Make rules
.PHONY: all clean dist pkg lib install

ifdef _PS3SDK_
all: $(TARGET_CELL).elf
else
all: $(TARGET).elf
endif

clean: 
	rm -f $(OBJ) $(LIBNAME).a $(TARGET).zip $(TARGET).self $(TARGET).elf $(CONTENTID).pkg EBOOT.BIN PARAM.SFO $(CELLLIB).a $(TARGET_CELL).elf
	rm -rf $(CONTENTID) $(BUILDDIR) objs

dist: clean $(TARGET).zip

pkg: $(CONTENTID).pkg

ifdef _PS3SDK_
lib: $(CELLLIB).a
else
lib: $(LIBNAME).a
endif

ifndef _PS3SDK_
install: lib
	cp -fr $(CURDIR)/ftp $(PORTLIBS)/include/
	cp -f $(LIBNAME).a $(PORTLIBS)/lib/
endif

$(LIBNAME).a: $(OBJ:main.o=)
	$(AR) -rc $@ $^
	$(PREFIX)ranlib $@

$(CELLLIB).a: Makefile.celllib.mk
	$(MAKE) -f $< NAME=$(TARGET_CELL)

$(TARGET).zip: $(CONTENTID).pkg
	mkdir -p $(BUILDDIR)/npdrm $(BUILDDIR)/rex
	cp $< $(BUILDDIR)/npdrm/
	cp $< $(BUILDDIR)/rex/
	-$(PACKAGE_FINALIZE) $(BUILDDIR)/npdrm/$<
	zip -r9ql $(CURDIR)/$(TARGET).zip build readme.txt changelog.txt

ifdef _PS3SDK_
EBOOT.BIN: $(TARGET_CELL).elf
else
EBOOT.BIN: $(TARGET).elf
endif
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
	$(CXX) $< $(LDFLAGS) -o $@
	$(SPRX) $@

$(TARGET_CELL).self: $(TARGET_CELL).elf
	$(call MAKE_FSELF,$<,$@)

$(TARGET_CELL).elf: Makefile.cell.mk
	$(MAKE) -f $< NAME=$(TARGET_CELL)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -c $< -o $@
