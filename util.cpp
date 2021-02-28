#include <Arduino.h>
#include <Wire.h>
#include "mailstation.h"
#include "wifistation.h"

void
setup(void)
{
	uint16_t v;

	led_setup();
	ms_setup();

	WiFi.mode(WIFI_STA);
	WiFi.disconnect();

	Serial.begin(115200);

	delay(500);
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
	Serial.print(c);

	/* TODO: print to ms */
	return 0;
}

int
output(char *str)
{
	size_t len = strlen(str);
	int i, ret;

	for (i = 0; i < len; i++) {
		output(str[i]);
#if 0
		if ((ret = ms_write(str[i])) != 0)
			return ret;
#endif
	}

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
