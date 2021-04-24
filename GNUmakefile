SUBDIRS := esp8266 host mailstation

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for f in $(SUBDIRS); do $(MAKE) -C $$f clean; done

flash_esp8266: esp8266
	env UPLOAD_PORT=/dev/cuaU1 $(MAKE) -C esp8266 flash

.PHONY: all clean $(SUBDIRS)
