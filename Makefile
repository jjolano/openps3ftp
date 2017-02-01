# OpenPS3FTP Makefile

# SDK can be either PSL1GHT or CELL
SDK			?= PSL1GHT

ifeq ($(SDK),PSL1GHT)
ifeq ($(PSL1GHT),)
	$(error PSL1GHT not found.)
endif
include $(PSL1GHT)/ppu_rules
else
ifeq ($(SDK),CELL)
ifeq ($(CELL_SDK),)
	$(error CELL_SDK not found.)
endif
else
	$(error Unknown/unsupported SDK.)
endif
endif

ifeq ($(SDK),PSL1GHT)
TARGET		:= openps3ftp
else
TARGET		:= cellps3ftp
endif

LIBNAME		:= lib$(TARGET)

# Applies to PSL1GHT SDK compilations.
OPTS		:= -D_USE_SYSFS_ -D_USE_FASTPOLL_ -D_USE_IOBUFFERS_

# Define pkg file and application information
ifeq ($(SDK),PSL1GHT)
TITLE		:= OpenPS3FTP
APPID		:= NPXS91337
endif

ifeq ($(SDK),CELL)
TITLE		:= CellPS3FTP
APPID		:= NPXS01337
endif

CONTENTID	:= UP0001-$(APPID)_00-0000000000000000
SFOXML		:= $(CURDIR)/pkg-meta/sfo.xml
ICON0		:= $(CURDIR)/pkg-meta/ICON0.PNG

# Setup commands
# scetool: github.com/naehrwert/scetool
MAKE_SELF_NPDRM = scetool -0 SELF -1 TRUE -s FALSE -3 1010000001000003 -4 01000002 -5 NPDRM -A 0001000000000000 -6 0003004000000000 -b FREE -c EXEC -g EBOOT.BIN -f $(3) -2 $(4) -e $(1) $(2) -j TRUE
MAKE_PKG = $(PKG) --contentid $(3) $(1) $(2)
MAKE_FSELF = $(FSELF) $(1) $(2)
MAKE_SFO = $(SFO) --fromxml --title "$(3)" --appid "$(4)" $(1) $(2)

ifeq ($(SDK),PSL1GHT)

# Libraries
LIBPATHS	:= -L. -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -l$(TARGET) -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2 -lm -lz

# Includes
INCLUDE		:= -I. -I$(CURDIR)/ftp -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRC			:= client.cpp command.cpp server.cpp util.cpp main.cpp
OBJ			:= $(SRC:.cpp=.o)

# Define compilation options
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= -s $(MACHDEP) $(LIBPATHS) $(LIBS)

endif

# Make rules
.PHONY: all clean distclean dist pkg lib install

all: $(TARGET).elf

clean: 
	rm -f $(OBJ) $(LIBNAME).a $(TARGET).zip $(TARGET).self $(TARGET).elf $(CONTENTID).pkg EBOOT.BIN PARAM.SFO
	rm -rf $(CONTENTID) $(BUILDDIR) objs

distclean:
	$(MAKE) clean SDK=PSL1GHT
	$(MAKE) clean SDK=CELL

dist: distclean $(TARGET).zip

pkg: $(CONTENTID).pkg

lib: $(LIBNAME).a

ifeq ($(SDK),PSL1GHT)
install: lib
	cp -fr $(CURDIR)/ftp $(PORTLIBS)/include/
	cp -f $(LIBNAME).a $(PORTLIBS)/lib/
endif

ifeq ($(SDK),CELL)
install: lib
	cp -fr $(CURDIR)/ftp $(CELL_SDK)/target/ppu/include/
	cp -f $(LIBNAME).a $(CELL_SDK)/target/ppu/lib/
endif

ifeq ($(SDK),PSL1GHT)
$(LIBNAME).a: $(OBJ:main.o=)
	$(AR) -rc $@ $^
	$(PREFIX)ranlib $@
endif

ifeq ($(SDK),CELL)
$(LIBNAME).a: Makefile.celllib.mk
	$(MAKE) -f $< NAME=$(TARGET)
endif

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

ifeq ($(SDK),PSL1GHT)
$(TARGET).elf: main.o $(LIBNAME).a
	$(CXX) $< $(LDFLAGS) -o $@
	$(SPRX) $@
endif

ifeq ($(SDK),CELL)
$(TARGET).elf: Makefile.cell.mk
	$(MAKE) -f $< NAME=$(TARGET)
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -c $< -o $@
