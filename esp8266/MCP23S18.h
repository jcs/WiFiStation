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

#ifndef _MCP23S018_H_
#define _MCP23S018_H_

#include <Arduino.h>
#include <Wire.h>

class MCP23S18 {
public:
	uint8_t cs_pin;
	uint16_t pin_states;

	void begin(uint8_t _cs_pin);

	uint8_t readGPIO(uint8_t bank_b);
	uint16_t readGPIOAB(void);
	void writeGPIO(uint8_t bank_b, uint8_t val);

	uint8_t readRegister(uint8_t reg);
	void writeRegister(uint8_t addr, uint8_t b);
	void updateRegisterBit(uint8_t reg, uint8_t bit, bool set);
	void pinMode(uint8_t pin, uint8_t dir);
	void bankPinMode(uint8_t bank, uint8_t dir);

	uint8_t digitalRead(uint8_t b);
	void digitalWrite(uint8_t pin, uint8_t level);
	void pullUp(uint8_t pin, uint8_t level);
	void bankPullUp(uint8_t bank, uint8_t level);

private:
	inline uint8_t regForPin(uint8_t reg, uint8_t pin);
	inline uint8_t bitForPin(uint8_t pin);
	void beginSend(uint8_t mode);
	void endSend(void);
};

/* IOCON.BANK = 0 */
#define MCP23S18_IOCON_INIT	0x0A

/* IOCON.BANK = 1 */
#define MCP23S18_IODIR		0x00	/* I/O DIRECTION */
#define  MCP23S18_IODIR_OUTPUT		0x0
#define  MCP23S18_IODIR_INPUT		0x1
#define MCP23S18_IPOL		0x01	/* INPUT POLARITY */
#define MCP23S18_GPINTEN	0x02	/* INTERRUPT-ON-CHANGE CONTROL */
#define MCP23S18_DEFVAL		0x03	/* DEFAULT COMPARE REGISTER FOR INTERRUPT-ON-CHANGE */
#define MCP23S18_INTCON		0x04	/* INTERRUPT CONTROL */
#define MCP23S18_IOCON		0x05	/* CONFIGURATION */
#define  MCP23S18_IOCON_INTCC		(1 << 0) /* Interrupt Clearing Control */
#define  MCP23S18_IOCON_INTPOL		(1 << 1) /* polarity of the INT output */
#define  MCP23S18_IOCON_ODR		(1 << 2) /* INT pin as an open-drain output */
#define  MCP23S18_IOCON_SEQOP		(1 << 5) /* Sequential Operation mode */
#define  MCP23S18_IOCON_MIRROR		(1 << 6) /* INT pins mirror */
#define  MCP23S18_IOCON_BANK		(1 << 7) /* how the registers are addressed */
#define MCP23S18_GPPU		0x06	/* PULL-UP RESISTOR CONFIGURATION */
#define MCP23S18_INTF		0x07	/* INTERRUPT FLAG */
#define MCP23S18_INTCAP		0x08	/* INTERRUPT CAPTURE */
#define MCP23S18_GPIO		0x09	/* PORT */
#define MCP23S18_OLAT		0x0A	/* OUTPUT LATCH */

#define MCP23S18_BANKA		0x00
#define  MCP23S18_BANKA_MAX_PIN		7
#define MCP23S18_BANKB		0x10

#endif
