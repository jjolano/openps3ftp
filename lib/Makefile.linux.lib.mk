# libFTP++ Makefile

TARGET_LIB	:= libftpxx.a
PREFIX_DIR	?= /usr/local

# Includes
INCLUDE		:= -I../include

# Source Files
SRCS		:= $(wildcard *.cpp)
OBJS		:= $(SRCS:.cpp=.o)

# Define compilation options
DEFINES		:= -DLINUX
CXXFLAGS	+= -O2 -g -Wall $(INCLUDE) $(DEFINES)

# Make rules
.PHONY: all clean install

all: $(TARGET_LIB)

clean: 
	rm -f $(OBJS) $(TARGET_LIB)

install: all
	mkdir -p $(PREFIX_DIR)/include/ftp
	cp -f ../include/*.hpp $(PREFIX_DIR)/include/ftp/
	cp -f $(TARGET_LIB) $(PREFIX_DIR)/lib/

$(TARGET_LIB): $(OBJS)
	$(AR) rcs $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
