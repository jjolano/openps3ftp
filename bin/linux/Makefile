# FTP Makefile
GIT_VERSION := $(shell git describe --dirty --always)

TARGET		:= ftp.elf

# Libraries
LDFLAGS		+= -Wl,--gc-sections -L../../lib -m32
LDLIBS		+= -lftp

# Includes
INCLUDE		:= -I../../include -I../../feat

# Source Files
SRCS		:= $(wildcard *.cpp)
OBJS		:= $(SRCS:.cpp=.cpp.o)

C_SRCS		:= $(wildcard ../../feat/*/*.c)
OBJS		+= $(C_SRCS:.c=.c.o)

# Define compilation options
DEFINES		:= -DLINUX
CFLAGS		+= -ffunction-sections -fdata-sections -Og -g -Wall -m32 $(INCLUDE) $(DEFINES)
CFLAGS		+= -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
CFLAGS		+= -D_FILE_OFFSET_BITS=64
CXXFLAGS	+= -std=c++11 $(CFLAGS)

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

%.cpp.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.c.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
