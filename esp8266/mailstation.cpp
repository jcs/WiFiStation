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

#include "MCP23S18.h"
#include "wifistation.h"

/* millis to consider a read/write timed out */
#define MAILSTATION_TIMEOUT 2500

/*
 * ESP8266 default SPI pins:
 *
 * GPIO 12: MISO
 * GPIO 13: MOSI
 * GPIO 14: SCK
 *
 * Custom pins will be:
 * GPIO 4:  CS
 * GPIO 16: Reset
 */

#define GPIO_CS		4
#define GPIO_RESET	16

/* these are pins on the MCP23S18 */
const int pData0    =  0;
const int pData1    =  1;
const int pData2    =  2;
const int pData3    =  3;
const int pData4    =  4;
const int pData5    =  5;
const int pData6    =  6;
const int pData7    =  7;

const int pBusy     =  8; /* input from mailstation pin 1 (strobe) */
const int pAck      =  9; /* input from mailstation pin 14 (linefeed) */

const int pLineFeed = 10; /* output to mailstation pin 10 (ack) */
const int pStrobe   = 11; /* output to mailstation pin 11 (busy) */

MCP23S18 mcp;

/* cache data pin direction */
int data_mode = -1;

void
ms_init(void)
{
	/* reset the MCP23S18 */
	pinMode(GPIO_RESET, OUTPUT);
	digitalWrite(GPIO_RESET, LOW);
	delay(100);
	digitalWrite(GPIO_RESET, HIGH);
	delay(100);

	mcp.begin(GPIO_CS);

	/* data lines will flip between input/output, start in output mode */
	ms_datadir(OUTPUT);

	/* strobe (control) */
	mcp.pinMode(pStrobe, OUTPUT);
	mcp.pullUp(pStrobe, HIGH);
	mcp.digitalWrite(pStrobe, LOW);

	/* linefeed (control) */
	mcp.pinMode(pLineFeed, OUTPUT);
	mcp.pullUp(pLineFeed, HIGH);
	mcp.digitalWrite(pLineFeed, LOW);

	/* ack (status) */
	mcp.pinMode(pAck, INPUT);
	mcp.pullUp(pAck, LOW);

	/* busy (status) */
	mcp.pinMode(pBusy, INPUT);
	mcp.pullUp(pBusy, LOW);
}

void
ms_setup(void)
{
	uint8_t iocon;

	for (int i = 0; i < 10; i++) {
		ms_init();
		iocon = mcp.readRegister(MCP23S18_IOCON);

		if (iocon == 0xff || iocon == 0x0) {
			error_flash();
			ms_init();
		} else
			break;
	}
}

void
ms_datadir(uint8_t which)
{
	if (data_mode == which)
		return;

	mcp.bankPinMode(0, which);
	mcp.bankPullUp(0, which == OUTPUT ? HIGH : LOW);
	data_mode = which;
}

int
ms_read(void)
{
	unsigned long t;
	char c;

	/* sender raises strobe (busy) to signal a write */
	if (mcp.digitalRead(pBusy) == LOW)
		return -1;

	/* but when both lines are high, something's not right */
	if (mcp.digitalRead(pAck) == HIGH) {
		/*
		 * Both pins will be raised during a reboot, so in that case
		 * stop talking.
		 */
		mailstation_alive = false;
		return -1;
	}

	ms_datadir(INPUT);

	/* raise linefeed (ack) to signal ready to receive */
	mcp.digitalWrite(pLineFeed, HIGH);

	/* sender sees raised ack and writes data */

	/* sender lowers strobe (busy) once data is ready */
	t = millis();
	while (mcp.digitalRead(pBusy) == HIGH) {
		if (millis() - t > MAILSTATION_TIMEOUT) {
			mcp.digitalWrite(pLineFeed, LOW);
			error_flash();
			return -1;
		}
		ESP.wdtFeed();
	}

	c = mcp.readGPIO(0);

	/* lower linefeed (ack) when we're done reading */
	mcp.digitalWrite(pLineFeed, LOW);

	return c;
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

	ms_datadir(OUTPUT);

	/* raise strobe (busy on receiver) to signal write */
	mcp.digitalWrite(pStrobe, HIGH);

	/* wait for receiver to raise ack (linefeed on receiver) */
	t = millis();
	while (mcp.digitalRead(pAck) == LOW &&
	    mcp.digitalRead(pLineFeed) == LOW) {
		if (millis() - t > MAILSTATION_TIMEOUT) {
			mcp.digitalWrite(pStrobe, LOW);
			error_flash();
			mailstation_alive = false;
			return -1;
		}
		ESP.wdtFeed();
	}

	/* ack is high, write data */
	ms_writedata(c);

	/* lower strobe (busy on receiver) to indicate we're done writing */
	mcp.digitalWrite(pStrobe, LOW);

	/* wait for receiver to read and lower ack (busy on their end) */
	t = millis();
	while (mcp.digitalRead(pAck) == HIGH) {
		if (millis() - t > MAILSTATION_TIMEOUT) {
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
	int ret;

	for (size_t i = 0; i < len; i++)
		if ((ret = ms_write(str.charAt(i))) != 0)
			return ret;

	return 0;
}

int
ms_print(char *string)
{
	size_t len = strlen(string);
	int ret;

	for (size_t i = 0; i < len; i++)
		if ((ret = ms_write(string[i])) != 0)
			return ret;

	return 0;
}

void
ms_writedata(char c)
{
	mcp.writeGPIO(0, c);
}
