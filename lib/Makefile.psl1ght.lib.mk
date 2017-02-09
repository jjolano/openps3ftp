# libOpenPS3FTP Makefile

include $(PSL1GHT)/ppu_rules

TARGET_LIB	:= libopenps3ftp.a

# Includes
INCLUDE		:= -I../include -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)

# Source Files
SRCS		:= $(wildcard *.cpp)
OBJS		:= $(SRCS:.cpp=.o)

# Define compilation options
CXXFLAGS	= -O2 -g -mregnames -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CFLAGS		= $(CXXFLAGS)

# Make rules
.PHONY: all clean install

all: $(TARGET_LIB)

clean: 
	rm -f $(OBJS) $(TARGET_LIB)

install: lib
	mkdir -p $(PORTLIBS)/include/ftp
	cp -f ../include/*.hpp $(PORTLIBS)/include/ftp/
	cp -f $(TARGET_LIB) $(PORTLIBS)/lib/

$(TARGET_LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
