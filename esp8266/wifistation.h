#ifndef __WIFISTATION_H__
#define __WIFISTATION_H__

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <Wire.h>

#define EEPROM_SIZE		512
struct __attribute((__packed__)) eeprom_data {
	char magic[3]; /* "jcs" */
#define EEPROM_MAGIC_BYTES	"jcs"
	uint8_t revision;
	char wifi_ssid[64];
	char wifi_pass[64];
	uint32_t baud;
	char telnet_tterm[32];
	uint8_t telnet_tts_w;
	uint8_t telnet_tts_h;
	uint8_t telnet;
};

extern struct eeprom_data *settings;

/* these are ESP8266 pins */
const int pRedLED   =  0;
const int pBlueLED  =  2;

/* wifistation.ino */
void exec_cmd(char *cmd, size_t len);
extern bool serial_alive;
extern bool mailstation_alive;

/* util.cpp */
void led_setup(void);
void led_reset(void);
void error_flash(void);
size_t outputf(const char *format, ...);
int output(char c);
int output(char *str);
int output(String str);

/* mailstation.cpp */
void ms_setup(void);
void ms_datadir(uint8_t which);
int ms_read(void);
uint16_t ms_status(void);
int ms_write(char c);
int ms_print(char *string);
int ms_print(String);

#endif
