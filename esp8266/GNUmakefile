# pkg_add makeesparduino

# targeting the adafruit feather huzzah esp8266
BOARD = 	huzzah

# default of -w supresses all warnings
COMP_WARNINGS = -Wall -Wextra

BUILD_ROOT =	$(CURDIR)/obj
EXCLUDE_DIRS =	$(BUILD_ROOT)

LIBS=		Syslog

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),OpenBSD)
	ESP_ROOT =	/usr/local/share/arduino/hardware/espressif/esp8266
	ARDUINO_ROOT =	/usr/local/share/arduino
	ARDUINO_LIBS =	${ESP_ROOT}/libraries

	UPLOAD_PORT?=	/dev/cuaU0
	include /usr/local/share/makeEspArduino/makeEspArduino.mk
else
	include ../../makeEspArduino/makeEspArduino.mk
endif

ifeq ($(UNAME_S),OpenBSD)
SERVER_IP?=	`route -qn get 0.0.0.0 | grep 'address:' | sed 's/.*: //'`
endif
ifeq ($(UNAME_S),Darwin)
_IFACE=		$(shell route -qn get 0.0.0.0 | grep interface: | sed 's/.*: //')
SERVER_IP?=	$(shell ipconfig getifaddr ${_IFACE})
endif
SERVER_PORT?=	8000

serve_ota: all
	@[ -d ../release ] || mkdir ../release
	@grep _VERSION wifistation.h | sed -e 's/"$$//' -e 's/.*"//' > ../release/ota.txt
	@cp -f $(MAIN_EXE) ../release/update.bin
	@stat -f "%z" ../release/update.bin >> ../release/ota.txt
	@md5 -q ../release/update.bin >> ../release/ota.txt
	@echo http://$(SERVER_IP):$(SERVER_PORT)/update.bin >> ../release/ota.txt
	@echo ""
	@echo "Issue update command:"
	@echo ""
	@/bin/echo -n "AT$$"
	@echo "UPDATE! http://${SERVER_IP}:${SERVER_PORT}/ota.txt"
	@echo ""
	cd ../release && python3 -m http.server --bind $(SERVER_IP) $(SERVER_PORT)
