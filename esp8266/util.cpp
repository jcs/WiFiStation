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

#include "wifistation.h"

struct eeprom_data *settings;

bool serial_alive = true;
bool mailstation_alive = false;

WiFiUDP syslogUDPClient;
Syslog syslog(syslogUDPClient, SYSLOG_PROTO_BSD);

#define BOOKMARK_0	"klud.ge"

void
setup(void)
{
	static_assert(sizeof(struct eeprom_data) < EEPROM_SIZE,
	    "EEPROM_SIZE is not large enough to hold struct eeprom_data");

	EEPROM.begin(EEPROM_SIZE);
	settings = (struct eeprom_data *)EEPROM.getDataPtr();
	if (memcmp(settings->magic, EEPROM_MAGIC_BYTES,
	    sizeof(settings->magic)) == 0) {
		/* do migrations if needed based on current revision */
		switch (settings->revision) {
		case 1:
			settings->http_server = 0;
			/* FALLTHROUGH */
		case 2:
			memset(settings->bookmarks, 0,
			    BOOKMARK_SIZE * NUM_BOOKMARKS);
			strcpy(settings->bookmarks[0], BOOKMARK_0);
			/* FALLTHROUGH */
		case 3:
			settings->echo = 1;
			settings->quiet = 0;
			settings->verbal = 1;
		}

		if (settings->revision != EEPROM_REVISION) {
			settings->revision = EEPROM_REVISION;
			EEPROM.commit();
		}
	} else {
		/* start over */
		memset(settings, 0, sizeof(struct eeprom_data));
		memcpy(settings->magic, EEPROM_MAGIC_BYTES,
		    sizeof(settings->magic));
		settings->revision = EEPROM_REVISION;

		settings->baud = 115200;

		settings->telnet = 1;
		strlcpy(settings->telnet_tterm, "ansi",
		    sizeof(settings->telnet_tterm));
		/* msTERM defaults */
		settings->telnet_tts_w = 64;
		settings->telnet_tts_h = 15;

		settings->http_server = 0;

		settings->echo = 1;
		settings->quiet = 0;
		settings->verbal = 1;

		memset(settings->bookmarks, 0,
		    BOOKMARK_SIZE * NUM_BOOKMARKS);
		strcpy(settings->bookmarks[0], BOOKMARK_0);

		EEPROM.commit();
	}

	syslog_setup();

	Serial.begin(settings->baud);
	delay(1000);

	led_setup();
	ms_setup();
	led_reset();

	WiFi.mode(WIFI_STA);

	/* don't require wifi_pass in case it's an open network */
	if (settings->wifi_ssid[0] == 0)
		WiFi.disconnect();
	else
		WiFi.begin(settings->wifi_ssid, settings->wifi_pass);

	http_setup();
}

void
syslog_setup(void)
{
	if (settings->syslog_server[0])
		syslog.server(settings->syslog_server, 514);
	else
		syslog.server(NULL, 514);
}

void
led_setup(void)
{
	/* setup LEDs */
	pinMode(pRedLED, OUTPUT);
	pinMode(pBlueLED, OUTPUT);
	led_reset();
}

void
error_flash(void)
{
	digitalWrite(pRedLED, LOW);
	digitalWrite(pBlueLED, HIGH);
	delay(100);
	digitalWrite(pRedLED, HIGH);
	digitalWrite(pBlueLED, LOW);
	delay(100);
	digitalWrite(pBlueLED, HIGH);
}

void
led_reset(void)
{
	digitalWrite(pRedLED, HIGH);
	digitalWrite(pBlueLED, HIGH);
}

size_t
outputf(const char *format, ...)
{
	va_list arg;
	char temp[64];
	char* buf;

	va_start(arg, format);
	size_t len = vsnprintf(temp, sizeof(temp), format, arg);
	va_end(arg);

	if (len > sizeof(temp) - 1) {
		/* too big for stack buffer, malloc something bigger */
		buf = (char *)malloc(len + 1);
		if (!buf)
			return 0;

		va_start(arg, format);
		vsnprintf(buf, len + 1, format, arg);
		va_end(arg);
	} else
		buf = temp;

	output(buf);

	if (buf != temp)
		free(buf);

	return len;
}

int
output(char c)
{
	if (serial_alive) {
		Serial.write(c);
		if (c == '\n')
			Serial.flush();
	}
	if (mailstation_alive)
		ms_write(c);

	return 0;
}

int
output(const char *str)
{
	size_t len = strlen(str);

#ifdef OUTPUT_TRACE
	syslog.logf(LOG_DEBUG, "output: \"%s\"", str);
#endif

	for (size_t i = 0; i < len; i++)
		output(str[i]);

	return 0;
}

int
output(String str)
{
	size_t len = str.length();
	char *buf = (char *)malloc(len + 1);
	int ret;

	if (buf == NULL)
		return -1;

	str.toCharArray(buf, len);
	ret = output(buf);
	free(buf);

	return ret;
}
