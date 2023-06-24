/*
 * WiFiStation
 * Copyright (c) 2021 joshua stein <jcs@jcs.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef __WIFISTATION_H__
#define __WIFISTATION_H__

#include <Arduino.h>
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <Syslog.h>
#include <WiFiUdp.h>

#define WIFISTATION_VERSION	"0.7"

#define EEPROM_SIZE		512
struct __attribute((__packed__)) eeprom_data {
	char magic[3];
#define EEPROM_MAGIC_BYTES	"jcs"
	uint8_t revision;
#define EEPROM_REVISION		4
	char wifi_ssid[64];
	char wifi_pass[64];
	uint32_t baud;
	char telnet_tterm[32];
	uint8_t telnet_tts_w;
	uint8_t telnet_tts_h;
	uint8_t telnet;
	uint8_t http_server;
#define BOOKMARK_SIZE 64
#define NUM_BOOKMARKS 3
	char bookmarks[NUM_BOOKMARKS][BOOKMARK_SIZE];
	char syslog_server[64];
	uint8_t echo;
	uint8_t quiet;
	uint8_t verbal;
};

extern struct eeprom_data *settings;
extern Syslog syslog;

#define MAX_UPLOAD_SIZE (16 * 1024)

/* these are ESP8266 pins */
const int pRedLED   =  0;
const int pBlueLED  =  2;

/* wifistation.ino */
void exec_cmd(char *cmd, size_t len);
extern bool serial_alive;
extern bool mailstation_alive;

/* util.cpp */
void syslog_setup(void);
void led_setup(void);
void led_reset(void);
void error_flash(void);
size_t outputf(const char *format, ...);
int output(char c);
int output(const char *str);
int output(String str);

/* mailstation.cpp */
extern const int pData0;
extern const int pData1;
extern const int pData2;
extern const int pData3;
extern const int pData4;
extern const int pData5;
extern const int pData6;
extern const int pData7;
extern const int pBusy;
extern const int pAck;
extern const int pLineFeed;
extern const int pStrobe;
void ms_setup(void);
void ms_datadir(uint8_t which);
int ms_read(void);
uint16_t ms_status(void);
int ms_write(char c);
int ms_print(char *string);
int ms_print(String);
void ms_writedata(char c);

/* telnet.cpp */
int telnet_connect(char *host, uint16_t port);
bool telnet_connected(void);
void telnet_disconnect(void);
int telnet_read(void);
int telnet_write(char b);
int telnet_write(String s);

/* http.cpp */
void http_setup(void);
void http_process(void);

/* update.cpp */
void update_process(char *, bool, bool);

#endif
