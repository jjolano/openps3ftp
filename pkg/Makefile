include $(PSL1GHT)/ppu_rules

DIST_TARGET			?= openps3ftp
GIT_VERSION			:= $(shell git describe --always)

TITLE_ID			?= NPXS91337
KLICENSEE			?= 0x72F990788F9CFF745725F08E4C12838
SERVICE_PREFIX		?= UP0001
NP_COMMUNICATION_ID	?= $(TITLE_ID)_00
SERVICE_ID			?= $(SERVICE_PREFIX)-$(NP_COMMUNICATION_ID)
ENTITLEMENT_LABEL	?= 0000000000000000

CONTENT_ID			:= $(SERVICE_ID)-$(ENTITLEMENT_LABEL)
PKG_VER				?= 01.00

ifneq ($(CELL_SDK),)
MAKE_PKG			:= psn_package_npdrm.exe
else
MAKE_PKG			:= $(PKG) --contentid $(3) $(1) $(2)
endif

all: $(CONTENT_ID).pkg

dist: distclean
	@echo create distribution $(DIST_TARGET)-$(GIT_VERSION) ...
	mkdir -p dist/cex dist/rex
	-mv readme.txt dist/readme.txt
	-mv changelog.txt dist/changelog.txt
	$(MAKE) RETAIL_PKG=0 clean all
	cp $(CONTENT_ID).pkg dist/rex/
	$(MAKE) RETAIL_PKG=1 clean all
	cp $(CONTENT_ID).pkg dist/cex/
	$(MAKE) clean
	zip -r9ql $(DIST_TARGET)-$(GIT_VERSION).zip dist

clean:
	rm -rf $(CONTENT_ID)
	rm -f $(CONTENT_ID).pkg

distclean: clean
	rm -rf dist
	rm -f $(DIST_TARGET)-$(GIT_VERSION).zip

PARAM.SFO: PARAM.SFO.xml
	$(SFO) --fromxml $< $@

package.conf:
	echo "Content-ID = $(CONTENT_ID)" > $@
	echo "klicensee = $(KLICENSEE)" >> $@
	echo "DRMType = Free" >> $@
	echo "ContentType = GameExec" >> $@
	echo "PackageVersion = $(PKG_VER)" >> $@

$(CONTENT_ID).pkg: EBOOT.BIN ICON0.PNG PARAM.SFO
	mkdir -p $(CONTENT_ID)/USRDIR
	cp EBOOT.BIN $(CONTENT_ID)/USRDIR/EBOOT.BIN
	-cp USRDIR/* $(CONTENT_ID)/USRDIR/
	cp ICON0.PNG $(CONTENT_ID)/ICON0.PNG
	cp PARAM.SFO $(CONTENT_ID)/PARAM.SFO
ifneq ($(CELL_SDK),)
	@echo using make_package_npdrm ...
	$(MAKE_PKG) package.conf $(CONTENT_ID)
else
	@echo using pkg.py ...
	$(call MAKE_PKG,$(CONTENT_ID)/,$@,$(CONTENT_ID))
endif
ifeq ($(RETAIL_PKG), 1)
	-$(PACKAGE_FINALIZE) $@
endif
