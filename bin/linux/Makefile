# FTP Makefile
GIT_VERSION := $(shell git describe --dirty --always)

TARGET		:= ftp.elf

# Libraries
LDFLAGS		+= -Wl,--gc-sections -L../../lib
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
CFLAGS		+= -ffunction-sections -fdata-sections -Og -g -Wall $(INCLUDE) $(DEFINES)
CFLAGS		+= -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
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
