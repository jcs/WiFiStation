#include "wifistation.h"

struct eeprom_data *settings;

bool serial_alive = false;
bool mailstation_alive = false;

void
setup(void)
{
	uint16_t v;

	EEPROM.begin(sizeof(struct eeprom_data));
	settings = (struct eeprom_data *)EEPROM.getDataPtr();
	if (memcmp(settings->magic, EEPROM_MAGIC_BYTES,
	    sizeof(settings->magic)) != 0) {
		/* start over */
		memset(settings, 0, sizeof(struct eeprom_data));
		memcpy(settings->magic, EEPROM_MAGIC_BYTES,
		    sizeof(settings->magic));
		settings->baud = 115200;
	}

	Serial.begin(settings->baud);
	delay(1000);

	led_setup();
	ms_setup();

	WiFi.mode(WIFI_STA);

	/* don't require wifi_pass in case it's an open network */
	if (settings->wifi_ssid[0] == 0)
		WiFi.disconnect();
	else
		WiFi.begin(settings->wifi_ssid, settings->wifi_pass);
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
	if (serial_alive)
		Serial.print(c);
	if (mailstation_alive)
		ms_write(c);

	return 0;
}

int
output(char *str)
{
	size_t len = strlen(str);
	int i, ret;

	for (i = 0; i < len; i++)
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
