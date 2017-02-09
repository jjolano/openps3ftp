# FTP++ Makefile

TARGET		:= ftpxx.elf

# Libraries
LDFLAGS		+= -L../lib
LDLIBS		+= -lftpxx

# Includes
INCLUDE		:= -I../include -I./helper -I./feat/base

# Source Files
SRCS		:= $(wildcard linux/*.cpp) $(wildcard helper/*.cpp) $(wildcard feat/*/*.cpp)
OBJS		:= $(SRCS:.cpp=.o)

# Define compilation options
DEFINES		:= -DLINUX
CXXFLAGS	+= -O2 -Wall $(INCLUDE) $(DEFINES)
LDFLAGS		+= -s

# Make rules
.PHONY: all clean

all: $(TARGET)

clean: 
	rm -f $(TARGET) $(OBJS)

$(TARGET): $(OBJS)
	$(CXX) $^ $(LDFLAGS) $(LDLIBS) -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
