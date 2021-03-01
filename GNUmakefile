SUBDIRS := esp8266 host mailstation

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@

flash_esp8266: esp8266
	env UPLOAD_PORT=/dev/cuaU1 gmake -C esp8266 flash

.PHONY: all $(SUBDIRS)
