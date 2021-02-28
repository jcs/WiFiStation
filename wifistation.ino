#include <Wire.h>

#include "Adafruit_MCP23017.h"

Adafruit_MCP23017 mcp;

/* this is a ESP8266 pin */
const int pRedLED   =  0;
const int pBlueLED  =  2;

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

/* cache data pin direction */
int data_mode;

void error_flash(void);
int msread(void);
int mswrite(char c);

void
setup()
{
	uint16_t v;

	/* led */
	pinMode(pRedLED, OUTPUT);
	digitalWrite(pRedLED, HIGH);
	pinMode(pBlueLED, OUTPUT);
	digitalWrite(pBlueLED, HIGH);

	/* use 1.7mhz i2c */
	Wire.setClock(1700000);
	mcp.begin(&Wire);

	/*
	 * check for MCP23017 returning all ones (or most likely, the i2c
	 * library returning all 1s) and flash lights until it's fixed
	 */
	while ((v = mcp.readGPIOAB()) && (v == 0xffff || v == 0xffffffff))
		error_flash();

	digitalWrite(pRedLED, HIGH);
	digitalWrite(pBlueLED, HIGH);

	/* data lines will flip between input/output, start in output mode */
	data_mode = OUTPUT;
	for (int i = 0; i <= 7; i++)
		mcp.pinMode(i, OUTPUT);

	/* strobe (control) */
	mcp.pinMode(pStrobe, OUTPUT);
	mcp.digitalWrite(pStrobe, LOW);

	/* linefeed (control) */
	mcp.pinMode(pLineFeed, OUTPUT);
	mcp.digitalWrite(pLineFeed, LOW);

	/* ack (status) */
	mcp.pinMode(pAck, INPUT);
	mcp.pullUp(pAck, LOW);

	/* busy (status) */
	mcp.pinMode(pBusy, INPUT);
	mcp.pullUp(pBusy, LOW);

	Serial.begin(115200);

	delay(500);
}

void
loop()
{
	int c;
	char b;

	if ((c = msread()) != -1)
		Serial.write(c);

	if (Serial.available()) {
		b = Serial.peek();

		if (mswrite(b) == 0) {
			/* remove it from the buffer */
			b = Serial.read();

			/* and echo it */
			Serial.write(b);
		}
	}
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
}

int
msread(void)
{
	unsigned long t;
	char c, c2;

	if (mcp.digitalRead(pBusy) != HIGH)
		return -1;

	/* but when both lines are high, something's not right */
	if (mcp.digitalRead(pAck) == HIGH)
		return -1;

	if (data_mode != INPUT) {
		for (int i = 0; i < 8; i++)
			mcp.pinMode(pData0 + i, INPUT);

		data_mode = INPUT;
	}

	mcp.digitalWrite(pLineFeed, HIGH);

	t = millis();
	while (mcp.digitalRead(pBusy) == HIGH) {
		if (millis() - t > 500) {
			mcp.digitalWrite(pLineFeed, LOW);
			error_flash();
			return -1;
		}
		yield();
	}

	c = mcp.readGPIO(0);

	mcp.digitalWrite(pLineFeed, LOW);

	/* invert and reverse data byte */
	c2 = 0;
	for (int i = 0; i < 8; i++)
		if (!(c & (1 << i)))
			c2 |= (1 << i);

	return c2;
}

int
mswrite(char c)
{
	unsigned long t;

	if (data_mode != OUTPUT) {
		for (int i = 0; i < 8; i++)
			mcp.pinMode(pData0 + i, OUTPUT);

		data_mode = OUTPUT;
	}

	mcp.digitalWrite(pStrobe, HIGH);

	t = millis();
	while (mcp.digitalRead(pAck) == LOW) {
		if (millis() - t > 500) {
			mcp.digitalWrite(pStrobe, LOW);
			error_flash();
			return -1;
		}
		ESP.wdtFeed();
	}

	/* write all data lines */
	mcp.writeGPIO(0, c);

	mcp.digitalWrite(pStrobe, LOW);

	t = millis();
	while (mcp.digitalRead(pAck) == HIGH) {
		if (millis() - t > 500)
			return -1;
		ESP.wdtFeed();
	}

	return 0;
}
