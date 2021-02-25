# pkg_add makeesparduino

# env UPLOAD_PORT=/dev/cuaU1 gmake flash

BUILD_ROOT = $(CURDIR)/obj
EXCLUDE_DIRS = $(BUILD_ROOT)

ESP_ROOT =  /usr/local/share/arduino/hardware/espressif/esp8266
ARDUINO_ROOT = /usr/local/share/arduino
ARDUINO_LIBS = ${ESP_ROOT}/libraries

include /usr/local/share/makeEspArduino/makeEspArduino.mk
