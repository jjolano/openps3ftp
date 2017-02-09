# OpenPS3FTP Makefile

include $(PSL1GHT)/ppu_rules

TARGET		:= openps3ftp.elf

# Libraries
LIBPATHS	:= -L../lib -L$(PORTLIBS)/lib $(LIBPSL1GHT_LIB)
LIBS		:= -lopenps3ftp -lNoRSX -lfreetype -lgcm_sys -lrsx -lnetctl -lnet -lsysutil -lsysmodule -lrt -lsysfs -llv2

# Includes
INCLUDE		:= -I../include -I$(PORTLIBS)/include/freetype2 -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= $(wildcard psl1ght/*.cpp)
OBJS		:= $(SRCS:.cpp=.o)

# Define compilation options
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
LDFLAGS		= -s $(MACHDEP) $(LIBPATHS) $(LIBS)

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $< $(LDFLAGS) -o $@
	$(SPRX) $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
