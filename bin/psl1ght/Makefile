# OpenPS3FTP Makefile
GIT_VERSION := $(shell git describe --always)
CONTENTID	?= UP0001-NPXS91337_00-0000000000000000

include $(PSL1GHT)/ppu_rules

TARGET		:= ftp.elf

# Libraries
LIBPATHS	:= -L../../lib -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lopenps3ftp_psl1ght -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2 -lm -lz

# Includes
INCLUDE		:= -I. -I../../include -I../../feat -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= $(wildcard *.cpp)
OBJS		= $(SRCS:.cpp=.cpp.ppu.o)

C_SRCS		:= $(wildcard ../../feat/*/*.c)
OBJS		+= $(C_SRCS:.c=.c.ppu.o)

# Define compilation options
DEFINES		= -DPSL1GHT_SDK
CXXFLAGS	= -ffunction-sections -fdata-sections -O2 -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) $(DEFINES)
CXXFLAGS	+= -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
CFLAGS		= $(CXXFLAGS)
LDFLAGS		= -Wl,--gc-sections -s $(MACHDEP) $(LIBPATHS) $(LIBS)

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

MAKE_FSELF = $(FSELF) $(1) $(2)
MAKE_SELF_NPDRM = scetool $(SCETOOL_FLAGS) -f $(3) -2 $(4) -e $(1) $(2)

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS) EBOOT.BIN

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(SPRX) $@

EBOOT.BIN: $(TARGET)
ifeq ($(UNSIGNED), 1)
	$(call MAKE_FSELF,$<,$@)
else
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)
endif

%.cpp.ppu.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.c.ppu.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
