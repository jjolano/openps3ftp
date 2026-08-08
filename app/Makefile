# OpenPS3FTP — new PS3 app (EBOOT)
GIT_VERSION := $(shell git describe --always)
CONTENTID	?= UP0001-NPXS91337_00-0000000000000000

include $(PSL1GHT)/ppu_rules

TARGET		:= ftp.elf

# Libraries
LIBPATHS	:= -L$(PORTLIBS)/lib -L$(PSL1GHT)/ppu/lib
LIBS		:= -lopenps3ftp -lmbedtls -lmbedx509 -lmbedcrypto \
		   -lsysfs -lnet -lnetctl -lsysmodule \
		   -lNoRSX -lfreetype -lpng -lgcm_sys -lrsx -lsysutil -lio \
		   -llv2 -lrt -lm -lz

# Includes
INCLUDE		:= -I. -I$(PORTLIBS)/include -I$(PORTLIBS)/include/freetype2 \
		   $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= main.c osd.cpp sysutil_compat.c ui.c log.c
OBJS		:= $(filter %.c.ppu.o,$(SRCS:.c=.c.ppu.o)) \
		   $(filter %.cpp.ppu.o,$(SRCS:.cpp=.cpp.ppu.o))

# Define compilation options
DEFINES		= -DPSL1GHT_SDK -DOPFTP_PS3=1
CXXFLAGS	= -ffunction-sections -fdata-sections -O2 -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) $(DEFINES)
CXXFLAGS	+= -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
CFLAGS		= $(CXXFLAGS)
# ps3dk's ld segfaults on --gc-sections (binutils bug)
LDFLAGS		= -s $(MACHDEP) $(LIBPATHS) $(LIBS)

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
.PHONY: all clean headless

all: $(TARGET)

# Headless variant for the RPCS3 test harness: no OSD, no NoRSX/RSX
# graphics stack — just the FTP server + printf status lines, which
# RPCS3 captures as sys_tty output.
headless: headless.elf

headless.elf: main.c
	$(CC) $(CFLAGS) -DOPFTP_HEADLESS=1 -c main.c -o main.headless.ppu.o
	$(CC) $(CFLAGS) -c log.c -o log.headless.ppu.o
	$(CXX) main.headless.ppu.o log.headless.ppu.o \
		-s $(MACHDEP) \
		-L$(PORTLIBS)/lib -L$(PSL1GHT)/ppu/lib \
		-lopenps3ftp -lmbedtls -lmbedx509 -lmbedcrypto \
		-lsysfs -lnet_stub -lsysmodule -llv2 -lrt -lm -lz \
		-o $@
	$(SPRX) $@

clean:
	rm -f $(TARGET) $(OBJS) EBOOT.BIN headless.elf main.headless.ppu.o log.headless.ppu.o

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(SPRX) $@

EBOOT.BIN: $(TARGET)
ifeq ($(UNSIGNED), 1)
	$(call MAKE_FSELF,$<,$@)
else
	$(call MAKE_SELF_NPDRM,$<,$@,$(CONTENTID),04)
endif

%.c.ppu.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.cpp.ppu.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
