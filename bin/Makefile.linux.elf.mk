# FTP Makefile
TARGET		:= ftp.elf

# Libraries
LDFLAGS		+= -L../lib
LDLIBS		+= -lftp

# Includes
INCLUDE		:= -I../include -I./feat

# Source Files
SRCS		:= $(wildcard linux/*.cpp)
OBJS		:= $(SRCS:.cpp=.cc.o)

C_SRCS		:= $(wildcard feat/*/*.c)
OBJS		+= $(C_SRCS:.c=.c.o)

# Define compilation options
DEFINES		:= -DLINUX
CFLAGS		+= -O2 -Wall $(INCLUDE) $(DEFINES)
CXXFLAGS	+= $(CFLAGS)

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

%.cc.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.c.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@
