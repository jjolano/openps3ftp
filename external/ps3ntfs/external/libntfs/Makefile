CELL_SDK ?= /c/cell
CELL_MK_DIR ?= $(CELL_SDK)/samples/mk

include $(CELL_MK_DIR)/sdk.makedef.mk

MK_TARGET = libntfs.mk libntfs_prx.mk

standard:         
	$(MAKE) --no-print-directory -f libntfs.mk

prx:         
	$(MAKE) --no-print-directory -f libntfs_prx.mk

include $(CELL_MK_DIR)/sdk.target.mk
