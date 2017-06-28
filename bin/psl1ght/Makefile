# OpenPS3FTP Makefile
GIT_VERSION := $(shell git describe --dirty --always)

include $(PSL1GHT)/ppu_rules

TARGET		:= openps3ftp.elf

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
CXXFLAGS	+= -D_FILE_OFFSET_BITS=64
CFLAGS		= $(CXXFLAGS)
LDFLAGS		= -Wl,--gc-sections -s $(MACHDEP) $(LIBPATHS) $(LIBS)

# NTFS support
ifeq ($(NTFS_SUPPORT), 1)
LIBPATHS	+= -L../../external/libntfs_ext
LIBS		+= -lntfs_ext

INCLUDE		+= -I../../external/libntfs_ext/include

C_SRCS		+= ps3ntfs.c
DEFINES		+= -D_NTFS_SUPPORT_
endif

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(SPRX) $@

%.cpp.ppu.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.c.ppu.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
