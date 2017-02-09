# libOpenPS3FTP Makefile

include $(PSL1GHT)/ppu_rules

TARGET_LIB	:= libopenps3ftp.a

# Includes
INCLUDE		:= -I../include -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= $(wildcard *.cpp)
OBJS		:= $(SRCS:.cpp=.ppu.o)

# Define compilation options
DEFINES		:= -DPSL1GHT_SDK
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE) $(DEFINES)

# Make rules
.PHONY: all clean install

all: $(TARGET_LIB)

clean: 
	rm -f $(OBJS) $(TARGET_LIB)

install: all
	mkdir -p $(PORTLIBS)/include/ftp
	cp -f ../include/*.hpp $(PORTLIBS)/include/ftp/
	cp -f $(TARGET_LIB) $(PORTLIBS)/lib/

$(TARGET_LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
