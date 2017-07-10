TYPE ?= psl1ght
SSFTP ?= $(CURDIR)/external/libssftp

all: lib
	$(MAKE) -C host/$(TYPE)

lib:
	$(MAKE) -C $(SSFTP) TYPE=$(TYPE) all

ifeq ($(TYPE),psl1ght)
dist: all
	$(MAKE) -C host/$(TYPE) pkg
	$(MAKE) -C host/$(TYPE) rpkg
endif

clean:
	$(MAKE) -C host/$(TYPE) clean
	$(MAKE) -C $(SSFTP) TYPE=$(TYPE) clean

distclean:
	$(MAKE) -C host/$(TYPE) distclean
	$(MAKE) -C $(SSFTP) TYPE=$(TYPE) distclean
