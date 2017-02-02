# OpenPS3FTP Makefile

# SDK can be either PSL1GHT or CELL
SDK			?= PSL1GHT

ifeq ($(PSL1GHT),)
	$(error PSL1GHT not found.)
endif

include $(PSL1GHT)/ppu_rules

ifeq ($(SDK),CELL)
ifeq ($(CELL_SDK),)
	$(error CELL_SDK not found.)
endif
endif

ifeq ($(SDK),PSL1GHT)
TARGET		:= openps3ftp
else
TARGET		:= cellps3ftp
endif

LIBNAME		:= lib$(TARGET)

# Applies to PSL1GHT SDK compilations.
OPTS		?= -D_USE_SYSFS_ -D_USE_FASTPOLL_ -D_USE_IOBUFFERS_ $(EXTRAOPTS)

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

ifeq ($(SDK),PSL1GHT)

# Libraries
LIBPATHS	:= -L. -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -l$(TARGET) -lNoRSX -lfreetype -lpixman-1 -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2 -lm -lz

# Includes
INCLUDE		:= -I. -I$(CURDIR)/ftp -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRC			:= client.cpp command.cpp server.cpp util.cpp main.cpp
OBJ			:= $(SRC:.cpp=.o)
LIBOBJ		:= $(filter-out main.o, $(OBJ))

# Define compilation options
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= -s $(MACHDEP) $(LIBPATHS) $(LIBS)

endif

# Make rules
.PHONY: all clean distclean dist pkg lib install PARAM.SFO

all: $(TARGET).elf

clean: 
	rm -f $(OBJ) $(LIBNAME).a $(TARGET).zip $(TARGET).self $(TARGET).elf $(APPID).pkg EBOOT.BIN PARAM.SFO
	rm -rf $(APPID) $(BUILDDIR) objs

distclean:
	$(MAKE) clean SDK=PSL1GHT
	$(MAKE) clean SDK=CELL

dist: clean $(TARGET).zip

sdkdist: distclean
	$(MAKE) dist EXTRAOPTS=$(EXTRAOPTS)
	$(MAKE) dist SDK=CELL

pkg: $(APPID).pkg

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
$(LIBNAME).a: $(LIBOBJ)
	$(AR) -rc $@ $^
	$(PREFIX)ranlib $@
endif

ifeq ($(SDK),CELL)
$(LIBNAME).a:
	$(MAKE) -f Makefile.cell.lib.mk NAME=$(TARGET)
endif

$(TARGET).zip: $(APPID).pkg
	mkdir -p $(BUILDDIR)/cex $(BUILDDIR)/rex
	cp $< $(BUILDDIR)/cex/
	cp $< $(BUILDDIR)/rex/
	-$(PACKAGE_FINALIZE) $(BUILDDIR)/cex/$<
	zip -r9ql $(CURDIR)/$(TARGET).zip build readme.txt changelog.txt

EBOOT.BIN: $(TARGET).elf
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)

$(APPID).pkg: EBOOT.BIN PARAM.SFO $(ICON0)
	mkdir -p $(APPID)/USRDIR
	cp $(ICON0) $(APPID)/
	cp PARAM.SFO $(APPID)/
	cp EBOOT.BIN $(APPID)/USRDIR/
	$(call MAKE_PKG,$(APPID)/,$@,$(CONTENTID))

PARAM.SFO: $(SFOXML)
	$(call MAKE_SFO,$<,$@,$(TITLE),$(APPID))

$(TARGET).self: $(TARGET).elf
ifeq ($(SDK),PSL1GHT)
	$(call MAKE_FSELF,$<,$@)
endif

ifeq ($(SDK),PSL1GHT)
$(TARGET).elf: main.o $(LIBNAME).a
	$(CXX) $< $(LDFLAGS) -o $@
	$(SPRX) $@
endif

ifeq ($(SDK),CELL)
$(TARGET).elf: $(LIBNAME).a
	$(MAKE) -f Makefile.cell.elf.mk NAME=$(TARGET)
endif

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(OPTS) -c $< -o $@
