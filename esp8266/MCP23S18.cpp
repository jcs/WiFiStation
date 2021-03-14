/*
 * MCP23S18 16-bit I/O Expander
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
#include <SPI.h>

/*
      .---._.---.
  VSS |  1   28 | NC
   NC |  2   27 | GPA7
 GPB0 |  3   26 | GPA6
 GPB1 |  4   25 | GPA5
 GPB2 |  5   24 | GPA4
 GPB3 |  6   23 | GPA3
 GPB4 |  7   22 | GPA2
 GPB5 |  8   21 | GPA1
 GPB6 |  9   20 | GPA0
 GPB7 | 10   19 | INTA
  VDD | 11   18 | INTB
   CS | 12   17 | NC
  SCK | 13   16 | RESET
 MOSI | 14   15 | MISO
      `---------'
*/

#define WRITE	0
#define READ	1

void
MCP23S18::begin(uint8_t _cs_pin)
{
	cs_pin = _cs_pin;
	::pinMode(cs_pin, OUTPUT);
	::digitalWrite(cs_pin, HIGH);

	SPI.begin();

	/*
	 * Disable Sequential Operation Mode and set BANK=1, but use the IOCON
	 * register at the address it's at when in BANK=0 as it is at power-on
	 */
	writeRegister(MCP23S18_IOCON_INIT,
	    MCP23S18_IOCON_SEQOP | MCP23S18_IOCON_BANK);
}

uint8_t
MCP23S18::readGPIO(uint8_t bank_b)
{
	return readRegister(MCP23S18_GPIO + (bank_b ? MCP23S18_BANKB : 0));
}

void
MCP23S18::writeGPIO(uint8_t bank_b, uint8_t val)
{
	writeRegister(MCP23S18_GPIO + (bank_b ? MCP23S18_BANKB : 0), val);
}

uint16_t
MCP23S18::readGPIOAB(void)
{
	return (readRegister(MCP23S18_BANKB + MCP23S18_GPIO) << 8) |
	    readRegister(MCP23S18_GPIO);
}

void
MCP23S18::updateRegisterBit(uint8_t reg, uint8_t bit, bool set)
{
	uint8_t val = readRegister(reg);

	if (set)
		val |= (1 << bit);
	else
		val &= ~(1 << bit);

	writeRegister(reg, val);
}

void
MCP23S18::pinMode(uint8_t pin, uint8_t dir)
{
	updateRegisterBit(regForPin(MCP23S18_IODIR, pin), bitForPin(pin),
	    dir == INPUT ? MCP23S18_IODIR_INPUT : MCP23S18_IODIR_OUTPUT);
}

void
MCP23S18::bankPinMode(uint8_t bank, uint8_t dir)
{
	writeRegister(MCP23S18_IODIR + (bank ? MCP23S18_BANKB : 0),
	    dir == OUTPUT ? 0 : 0xff);
}

uint8_t
MCP23S18::digitalRead(uint8_t pin)
{
	uint8_t val = readRegister(regForPin(MCP23S18_GPIO, pin));
	return (val & (1 << bitForPin(pin))) ? HIGH : LOW;
}

void
MCP23S18::digitalWrite(uint8_t pin, uint8_t level)
{
	uint8_t val = readRegister(regForPin(MCP23S18_OLAT, pin));

	if (level == HIGH)
		val |= (1 << bitForPin(pin));
	else
		val &= ~(1 << bitForPin(pin));

	writeRegister(regForPin(MCP23S18_OLAT, pin), val);
}

void
MCP23S18::pullUp(uint8_t pin, uint8_t level)
{
	updateRegisterBit(regForPin(MCP23S18_GPPU, pin), bitForPin(pin),
	    level == HIGH ? 1 : 0);
}

void
MCP23S18::bankPullUp(uint8_t bank, uint8_t level)
{
	writeRegister(MCP23S18_GPPU + (bank ? MCP23S18_BANKB : 0),
	    level == HIGH ? 0xff : 0);
}

uint8_t
MCP23S18::readRegister(uint8_t reg)
{
	uint8_t val;

	beginSend(READ);
	SPI.transfer(reg);
	val = SPI.transfer(0x0);
	endSend();
	return val;
}

void
MCP23S18::writeRegister(uint8_t reg, uint8_t b)
{
	beginSend(WRITE);
	SPI.transfer(reg);
	SPI.transfer(b);
	endSend();
}

void
MCP23S18::beginSend(uint8_t mode)
{
	/* 5Mhz */
	SPI.beginTransaction(SPISettings(5000000, MSBFIRST, SPI_MODE0));
	::digitalWrite(cs_pin, LOW);

	/* 7-bit address is 0x20, shift in the read/write bit */
	SPI.transfer((0x20 << 1) | mode);
}

void
MCP23S18::endSend(void)
{
	::digitalWrite(cs_pin, HIGH);
	SPI.endTransaction();
}

inline uint8_t
MCP23S18::regForPin(uint8_t reg, uint8_t pin)
{
	return reg + (pin > MCP23S18_BANKA_MAX_PIN ? MCP23S18_BANKB : 0);
}

inline uint8_t
MCP23S18::bitForPin(uint8_t pin)
{
	return pin % 8;
}
