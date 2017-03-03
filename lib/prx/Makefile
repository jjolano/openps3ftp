# libOpenPS3FTP Makefile
SDK ?= linux
GIT_VERSION := $(shell git describe --abbrev=5 --dirty --always)

ifeq ($(SDK), cell)
CELL_SDK ?= /usr/local/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk
include $(CELL_MK_DIR)/sdk.makedef.mk
else
LIB_EXTERNAL := 1
PPU_OBJS = $(PPU_SRCS:.c=.$(SDK).o)
endif

PPU_SRCS = $(wildcard *.c)
PPU_CFLAGS += -DAPP_VER_MIN=\"-g$(GIT_VERSION)\"
PPU_LIB_TARGET = libopenps3ftp_vsh.a

ifeq ($(LIB_EXTERNAL), 1)
PPU_CFLAGS += -D_NO_VSH_EXPORTS_
PPU_LIB_TARGET = libopenps3ftp.a
else
OBJS_DIR = objs/vsh
endif

PPU_OPTIMIZE_LV = -Os
PPU_INCDIRS += -I../../include/prx -I../../include/prx/ftp

ifeq ($(SDK), psl1ght)
include $(PSL1GHT)/ppu_rules
PPU_INCDIRS += -I$(PORTLIBS)/include $(LIBPSL1GHT_INC)
PPU_CFLAGS += -DPSL1GHT_SDK -g -Wall -mregnames -mcpu=cell $(MACHDEP)
PPU_LIB_TARGET = libopenps3ftp_psl1ght.a
endif

ifeq ($(SDK), linux)
PPU_CFLAGS += -DLINUX -g -Wall
PPU_LIB_TARGET = libftp.a
endif

ifeq ($(SDK), cell)
PPU_CFLAGS += -DCELL_SDK
include $(CELL_MK_DIR)/sdk.target.mk
else
.PHONY: all clean

all: $(PPU_LIB_TARGET)

clean:
	rm -f $(PPU_OBJS) $(PPU_LIB_TARGET)

$(PPU_LIB_TARGET): $(PPU_OBJS)
	$(AR) rcs $@ $^

%.$(SDK).o: %.c
	$(CC) $(PPU_OPTIMIZE_LV) $(PPU_CFLAGS) $(PPU_INCDIRS) -c $< -o $@
endif
