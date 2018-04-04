# OpenPS3FTP Makefile

# Make rules
.PHONY: all clean distclean native-lib native-elf native-clean ps3-lib ps3-elf ps3-eboot ps3-prxlib ps3-prx ps3-pkg ps3-dist ps3-clean ps3-distclean

all: native-elf

clean: native-clean ps3-clean

distclean: clean ps3-distclean

native-lib:
	$(MAKE) -C lib SDK=linux

native-elf: native-lib
	$(MAKE) -C bin/linux

native-clean: 
	$(MAKE) -C lib SDK=linux clean
	$(MAKE) -C bin/linux clean

ps3-lib: 
ifneq ($(PSL1GHT),)
	$(MAKE) -C lib SDK=psl1ght LIB_EXTERNAL=1
endif
ifneq ($(CELL_SDK),)
	$(MAKE) -C lib SDK=cell LIB_EXTERNAL=1
endif

ps3-elf: ps3-lib
ifneq ($(PSL1GHT),)
	$(MAKE) -C bin/psl1ght
endif

ps3-eboot: ps3-elf
ifneq ($(PSL1GHT),)
	$(MAKE) -C bin/psl1ght EBOOT.BIN
endif

ifneq ($(CELL_SDK),)
ps3-prxlib: 
	$(MAKE) -C lib SDK=cell

ps3-prx: ps3-prxlib
ifeq ($(NTFS_SUPPORT), 1)
	$(MAKE) -C external/ps3ntfs
endif
	$(MAKE) -C bin/prx
endif

ps3-pkg: ps3-eboot
	cp bin/psl1ght/EBOOT.BIN pkg/EBOOT.BIN
	$(MAKE) -C pkg

ps3-dist: ps3-eboot
	cp readme.txt pkg/readme.txt
	cp changelog.txt pkg/changelog.txt
	cp bin/psl1ght/EBOOT.BIN pkg/EBOOT.BIN
	$(MAKE) -C pkg dist

ps3-clean: 
ifneq ($(PSL1GHT),)
	$(MAKE) -C lib SDK=psl1ght LIB_EXTERNAL=1 clean
	$(MAKE) -C bin/psl1ght clean
endif
ifneq ($(CELL_SDK),)
	$(MAKE) -C external/ps3ntfs clean
	$(MAKE) -C lib SDK=cell LIB_EXTERNAL=1 clean
	$(MAKE) -C lib SDK=cell clean
	$(MAKE) -C bin/cell clean
	$(MAKE) -C bin/prx clean
endif

ps3-distclean: ps3-clean
	$(MAKE) -C pkg distclean
