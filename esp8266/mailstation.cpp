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

#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_MCP23017.h"
#include "wifistation.h"

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

Adafruit_MCP23017 mcp;

/* cache data pin direction */
int data_mode;

void
ms_setup(void)
{
	uint16_t v;

	/* use 1.7mhz i2c */
	Wire.setClock(1700000);
	mcp.begin(&Wire);

	/*
	 * check for MCP23017 returning all ones (or most likely, the i2c
	 * library returning all 1s) and flash lights until it's fixed
	 */
	while ((v = mcp.readGPIOAB()) && (v == 0xffff || v == 0xffffffff))
		error_flash();

	led_reset();

	/* data lines will flip between input/output, start in output mode */
	ms_datadir(OUTPUT);

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
}

void
ms_datadir(uint8_t which)
{
	for (int i = 0; i < 8; i++)
		mcp.pinMode(pData0 + i, which);

	data_mode = which;
}

int
ms_read(void)
{
	unsigned long t;
	char c, c2;

	if (mcp.digitalRead(pBusy) != HIGH)
		return -1;

	/* but when both lines are high, something's not right */
	if (mcp.digitalRead(pAck) == HIGH)
		return -1;

	if (data_mode != INPUT)
		ms_datadir(INPUT);

	mcp.digitalWrite(pLineFeed, HIGH);

	t = millis();
	while (mcp.digitalRead(pBusy) == HIGH) {
		if (millis() - t > 500) {
			mcp.digitalWrite(pLineFeed, LOW);
			error_flash();
			return -1;
		}
		ESP.wdtFeed();
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

uint16_t
ms_status(void)
{
	return mcp.readGPIOAB();
}

int
ms_write(char c)
{
	unsigned long t;

	if (data_mode != OUTPUT)
		ms_datadir(OUTPUT);

	mcp.digitalWrite(pStrobe, HIGH);

	t = millis();
	while (mcp.digitalRead(pAck) == LOW) {
		if (millis() - t > 500) {
			mcp.digitalWrite(pStrobe, LOW);
			error_flash();
			mailstation_alive = false;
			return -1;
		}
		ESP.wdtFeed();
	}

	/* write all data lines */
	/* XXX: using mcp.writeGPIO is marginally faster, but it seems
	 * unreliable so transfer each pin individually */
	for (int i = 0; i < 8; i++)
		mcp.digitalWrite(pData0 + i, (c & (1 << i)) ? HIGH : LOW);

	mcp.digitalWrite(pStrobe, LOW);

	t = millis();
	while (mcp.digitalRead(pAck) == HIGH) {
		if (millis() - t > 500) {
			error_flash();
			return -1;
		}
		ESP.wdtFeed();
	}

	return 0;
}

int
ms_print(String str)
{
	size_t len = str.length();
	int i, ret;

	for (i = 0; i < len; i++) {
		if ((ret = ms_write(str.charAt(i))) != 0)
			return ret;
	}

	return 0;
}

int
ms_print(char *string)
{
	size_t len = strlen(string);
	int i, ret;

	for (i = 0; i < len; i++)
		if ((ret = ms_write(string[i])) != 0)
			return ret;

	return 0;
}
