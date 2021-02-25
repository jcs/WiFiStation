#include <Wire.h>

#include "Adafruit_MCP23017.h"

Adafruit_MCP23017 mcp;

/* this is a ESP8266 pin */
const int pLed      =  0;

/* these are pins on the MCP23017 */
const int pData0    =  0;
const int pData1    =  1;
const int pData2    =  2;
const int pData3    =  3;
const int pData4    =  4;
const int pData5    =  5;
const int pData6    =  6;
const int pData7    =  7;

const int pBusy     =  8; /* input from lpt pin 1 (strobe) */
const int pAck      =  9; /* input from lpt pin 14 (linefeed) */

const int pLineFeed = 10; /* output to lpt pin 10 (ack) */
const int pStrobe   = 11; /* output to lpt pin 11 (busy) */

int
msread(void)
{
	unsigned long t;
	char c;

	if (mcp.digitalRead(pBusy) == HIGH)
		return - 1;

	for (int i = 0; i < 8; i++)
		mcp.pinMode(pData0 + i, INPUT);

	mcp.digitalWrite(pLineFeed, LOW);

	t = millis();
	while (mcp.digitalRead(pBusy) == LOW) {
		if (millis() - t > 1000) {
			mcp.digitalWrite(pLineFeed, HIGH);
			return -1;
		}
	}

	c = mcp.readGPIO(0);

	mcp.digitalWrite(pLineFeed, HIGH);

	return c;
}

int
mswrite(char c)
{
	unsigned long t;

	for (int i = 0; i < 8; i++)
		mcp.pinMode(pData0 + i, OUTPUT);

	mcp.digitalWrite(pStrobe, LOW);

	t = millis();
	while (digitalRead(pAck) == HIGH) {
		if (millis() - t > 1000) {
			mcp.digitalWrite(pStrobe, HIGH);
			return -1;
		}
	}

	/* TODO: implement Adafruit_MCP23017::writeGPIO to write atomically */
	for (int i = 0; i < 8; i++)
		mcp.digitalWrite(pData0 + i, (c & (1 << i)) ? HIGH : LOW);

	mcp.digitalWrite(pStrobe, HIGH);

	t = millis();
	while (mcp.digitalRead(pAck) == LOW) {
		if (millis() - t > 1000) {
			/* do we consider this a failure? */
			return 0;
		}
	}

	return 0;
}

void
setup()
{
	/* led */
	pinMode(pLed, OUTPUT);
	digitalWrite(pLed, LOW);

	/* use default i2c address */
	mcp.begin();

	/* data lines will flip between input/output */
	for (int i = 0; i < 16; i++)
		mcp.pinMode(i, INPUT);

	/* strobe (control) */
	mcp.pinMode(pStrobe, OUTPUT);

	/* linefeed (control) */
	mcp.pinMode(pLineFeed, OUTPUT);

	/* ack (status) */
	mcp.pinMode(pAck, INPUT);
	mcp.pullUp(pAck, HIGH);

	/* busy (status) */
	mcp.pinMode(pBusy, INPUT);
	mcp.pullUp(pBusy, HIGH);

	/* start with both control high */
	mcp.digitalWrite(pStrobe, HIGH);
	mcp.digitalWrite(pLineFeed, HIGH);

	Serial.begin(115200);
	while (Serial.available())
		Serial.read();

	delay(500);
}

void
loop()
{
	int c;

	while ((c = msread()) != -1)
		Serial.write((char)c);

	while (Serial.available()) {
		char j = Serial.peek();

		if (mswrite(j) == 0)
			/* remove it from the buffer */
			Serial.read();
	}
}
