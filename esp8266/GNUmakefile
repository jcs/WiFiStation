# pkg_add makeesparduino

# targeting the adafruit feather huzzah esp8266
BOARD = 	huzzah

# default of -w supresses all warnings
COMP_WARNINGS = -Wall -Wextra

BUILD_ROOT =	$(CURDIR)/obj
EXCLUDE_DIRS =	$(BUILD_ROOT)

ESP_ROOT =	/usr/local/share/arduino/hardware/espressif/esp8266
ARDUINO_ROOT =	/usr/local/share/arduino
ARDUINO_LIBS =	${ESP_ROOT}/libraries

UPLOAD_PORT?=	/dev/cuaU0

include /usr/local/share/makeEspArduino/makeEspArduino.mk
