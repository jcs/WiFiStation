SUBDIRS := esp8266 host mailstation

DOWNLOAD_URL := "https://raw.githubusercontent.com/jcs/WiFiStation/main/release/wifistation.bin"
VERSION := $(shell grep '#define WIFISTATION_VERSION' esp8266/wifistation.h | sed -e 's/"$$//' -e 's/.*"//')

all: $(SUBDIRS)
$(SUBDIRS):
	$(MAKE) -C $@

clean:
	for f in $(SUBDIRS); do $(MAKE) -C $$f clean; done

flash_esp8266: esp8266
	env UPLOAD_PORT=/dev/cuaU0 $(MAKE) -C esp8266 flash

release: all
	cp -f esp8266/obj/wifistation_huzzah/wifistation.bin release/
	cp -f mailstation/wsloader.bin release/
	cp -f mailstation/flashloader.bin release/
	echo $(VERSION) > release/version.txt
	stat -f "%z" release/wifistation.bin >> release/version.txt
	md5 -q release/wifistation.bin >> release/version.txt
	echo $(DOWNLOAD_URL) >> release/version.txt

.PHONY: all clean $(SUBDIRS)
