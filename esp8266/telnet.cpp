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

WiFiClient telnet;

enum {
	TELNET_STATE_DISCONNECTED = 0,
	TELNET_STATE_CONNECTED,
	TELNET_STATE_IAC,
	TELNET_STATE_IAC_WILL,
	TELNET_STATE_IAC_WONT,
	TELNET_STATE_IAC_DO,
	TELNET_STATE_IAC_DONT,
	TELNET_STATE_IAC_SB,
};

uint8_t telnet_state = TELNET_STATE_DISCONNECTED;
uint8_t telnet_sb[64];
uint8_t telnet_sb_len = 0;

#define SE		240	/* end of sub-negotiation options */
#define SB		250	/* start of sub-negotiation options */
#define WILL		251	/* confirm willingness to negotiate */
#define WONT		252	/* confirm unwillingness to negotiate */
#define DO		253	/* indicate willingness to negotiate */
#define DONT		254	/* indicate unwillingness to negotiate */
#define IAC		255	/* start of a negotiation sequence */

#define IS		0	/* sub-negotiation */
#define SEND		1	/* sub-negotiation */

#define IAC_BINARY	0	/* Transmit Binary */
#define IAC_ECHO	1	/* Echo Option */
#define IAC_SGA		3	/* Suppress Go Ahead Option */
#define IAC_STATUS	5	/* Status Option */
#define IAC_TM		6	/* Timing Mark Option */
#define IAC_NAOCRD	10	/* Output Carriage-Return Disposition Option */
#define IAC_NAOHTS	11	/* Output Horizontal Tabstops Option */
#define IAC_NAOHTD	12	/* Output Horizontal Tab Disposition Option */
#define IAC_NAOFFD	13	/* Output Formfeed Disposition Option */
#define IAC_NAOVTS	14	/* Output Vertical Tabstops Option */
#define IAC_NAOVTD	15	/* Output Vertical Tab Disposition Option */
#define IAC_NAOLFD	16	/* Output Linefeed Disposition */
#define IAC_XASCII	17	/* Extended Ascii Option */
#define IAC_LOGOUT	18	/* Logout Option */
#define IAC_BM		19	/* Byte Macro Option */
#define IAC_SUPDUP	22	/* SUPDUP-OUTPUT Option */
#define IAC_SENDLOC	23	/* SEND-LOCATION Option */
#define IAC_TTYPE	24	/* Terminal Type Option */
#define IAC_EOR		25	/* End of Record Option */
#define IAC_OUTMRK	27	/* Marking Telnet Option */
#define IAC_TTYLOC	28	/* Terminal Location Number Option */
#define IAC_DET		20	/* Data Entry Terminal Option DODIIS */
#define IAC_X3PAD	30	/* X.3 PAD Option */
#define IAC_NAWS	31	/* Window Size Option */
#define IAC_TSPEED	32	/* Terminal Speed Option */
#define IAC_FLOWCTRL	33	/* Remote Flow Control Option */
#define IAC_LINEMODE	34	/* Linemode Option */
#define IAC_XDISPLOC	35	/* X Display Location Option */
#define IAC_ENVIRON	36	/* Environment Option */
#define IAC_AUTH	37	/* Authentication */
#define IAC_ENCRYPT	38	/* Encryption Option */
#define IAC_NEWENV	39	/* Environment Option */
#define IAC_CHARSET	42	/* Charset Option */
#define IAC_COMPORT	44	/* Com Port Control Option */

#ifdef TELNET_IAC_TRACE
#define TELNET_IAC_DEBUG(...) { syslog.logf(LOG_INFO, __VA_ARGS__); delay(1); }
#else
#define TELNET_IAC_DEBUG(...) {}
#endif

#ifdef TELNET_DATA_TRACE
#define TELNET_DATA_DEBUG(...) { syslog.logf(LOG_INFO, __VA_ARGS__); delay(1); }
#else
#define TELNET_DATA_DEBUG(...) {}
#endif

int
telnet_connect(char *host, uint16_t port)
{
	if (telnet_state != TELNET_STATE_DISCONNECTED)
		telnet_disconnect();

	if (!telnet.connect(host, port))
		return 1;

	telnet.setNoDelay(true);

	telnet_state = TELNET_STATE_CONNECTED;

	if (settings->telnet) {
		/* start by sending things we support */
		TELNET_IAC_DEBUG("%s: -> IAC DO SUPPRESS GO AHEAD", __func__);
		telnet.printf("%c%c%c", IAC, DO, IAC_SGA);
		TELNET_IAC_DEBUG("%s: -> IAC WILL TTYPE", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_TTYPE);
		TELNET_IAC_DEBUG("%s: -> IAC WILL NAWS", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_NAWS);
		TELNET_IAC_DEBUG("%s: -> IAC WILL TSPEED", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_TSPEED);
		TELNET_IAC_DEBUG("%s: -> IAC WONT LINEMODE", __func__);
		telnet.printf("%c%c%c", IAC, WONT, IAC_LINEMODE);
		TELNET_IAC_DEBUG("%s: -> IAC DO STATUS", __func__);
		telnet.printf("%c%c%c", IAC, DO, IAC_STATUS);
	}

	return 0;
}

bool
telnet_connected(void)
{
	if (telnet_state == TELNET_STATE_DISCONNECTED)
		return false;

	if (!telnet.connected()) {
		if (telnet_state != TELNET_STATE_DISCONNECTED)
			telnet_disconnect();
		return false;
	}

	return true;
}

void
telnet_disconnect(void)
{
	telnet.stop();
	telnet_state = TELNET_STATE_DISCONNECTED;
}

#ifdef TELNET_IAC_TRACE
static char iac_name[16];
char *
telnet_iac_name(char iac)
{
	if (telnet_state == TELNET_STATE_CONNECTED && iac != IAC) {
		sprintf(iac_name, "%d (%c)", iac, iac);
		return iac_name;
	}

	switch (iac) {
	case IAC_ECHO:
		sprintf(iac_name, "ECHO");
		break;
	case IAC_SGA:
		sprintf(iac_name, "SGA");
		break;
	case IAC_STATUS:
		sprintf(iac_name, "STATUS");
		break;
	case IAC_TTYPE:
		sprintf(iac_name, "TTYPE");
		break;
	case IAC_NAWS:
		sprintf(iac_name, "NAWS");
		break;
	case IAC_TSPEED:
		sprintf(iac_name, "TSPEED");
		break;
	case IAC_FLOWCTRL:
		sprintf(iac_name, "FLOWCTRL");
		break;
	case IAC_LINEMODE:
		sprintf(iac_name, "LINEMODE");
		break;
	case IAC_ENCRYPT:
		sprintf(iac_name, "ENCRYPT");
		break;
	case IAC_AUTH:
		sprintf(iac_name, "AUTH");
		break;
	case IAC_XDISPLOC:
		sprintf(iac_name, "XDISPLOC");
		break;
	case IAC_NEWENV:
		sprintf(iac_name, "NEWENV");
		break;
	case IAC_ENVIRON:
		sprintf(iac_name, "OLD ENVIRON");
		break;
	case SE:
		sprintf(iac_name, "SE");
		break;
	case SB:
		sprintf(iac_name, "SB");
		break;
	case WILL:
		sprintf(iac_name, "WILL");
		break;
	case WONT:
		sprintf(iac_name, "WONT");
		break;
	case DO:
		sprintf(iac_name, "DO");
		break;
	case DONT:
		sprintf(iac_name, "DONT");
		break;
	case IAC:
		sprintf(iac_name, "IAC");
		break;
	default:
		sprintf(iac_name, "%d", iac);
		break;
	}
	return iac_name;
}
#endif

void
telnet_send_ttype(void)
{
	TELNET_IAC_DEBUG("%s: -> IAC SB TTYPE IS %s IAC SE", __func__,
	    settings->telnet_tterm);
	telnet.printf("%c%c%c%c", IAC, SB, IAC_TTYPE, IS);
	for (size_t i = 0; i < sizeof(settings->telnet_tterm); i++) {
		if (settings->telnet_tterm[i] == '\0')
			break;

		if (settings->telnet_tterm[i] == IAC)
			telnet.write(IAC);
		telnet.write(settings->telnet_tterm[i]);
	}
	telnet.printf("%c%c", IAC, SE);
}

void
telnet_send_naws(void)
{

	TELNET_IAC_DEBUG("%s: -> IAC SB NAWS IS %dx%d IAC SE", __func__,
	    settings->telnet_tts_w, settings->telnet_tts_h);

	telnet.printf("%c%c%c", IAC, SB, IAC_NAWS);

	/* we only support 8-bit settings, but NAWS is 16-bit * */
	telnet.write(0);
	if (settings->telnet_tts_w == IAC)
		telnet.write(IAC);
	telnet.write(settings->telnet_tts_w);

	telnet.write(0);
	if (settings->telnet_tts_h == IAC)
		telnet.write(IAC);
	telnet.write(settings->telnet_tts_h);

	telnet.printf("%c%c", IAC, SE);
}

void
telnet_send_tspeed(void)
{
	TELNET_IAC_DEBUG("%s: -> IAC SB TSPEED IS %d,%d IAC SE", __func__,
	    Serial.baudRate(), Serial.baudRate());

	telnet.printf("%c%c%c%c%d,%d", IAC, SB, IAC_TSPEED, IS,
	    Serial.baudRate(), Serial.baudRate());

	telnet.printf("%c%c", IAC, SE);
}

int
telnet_read(void)
{
	char b;

	if (!telnet.available())
		return -1;

	/* when AT$NET=0, just pass everything as-is */
	if (!settings->telnet)
		return telnet.read();

	b = telnet.peek();
	if (telnet_state != TELNET_STATE_CONNECTED || b == IAC)
		TELNET_IAC_DEBUG("telnet_read[%s]: %s",
		    (telnet_state == TELNET_STATE_CONNECTED ? "connected" :
		    (telnet_state == TELNET_STATE_IAC ? "IAC" :
		    (telnet_state == TELNET_STATE_IAC_WILL ? "WILL" :
		    (telnet_state == TELNET_STATE_IAC_WONT ? "WONT" :
		    (telnet_state == TELNET_STATE_IAC_DO ? "DO" :
		    (telnet_state == TELNET_STATE_IAC_DONT ? "DONT" :
		    (telnet_state == TELNET_STATE_IAC_SB ? "SB" : "?"))))))),
		    telnet_iac_name(b));

	switch (telnet_state) {
	case TELNET_STATE_CONNECTED:
		b = telnet.read();
		switch (b) {
		case IAC:
			telnet_state = TELNET_STATE_IAC;
			break;
		default:
			return b;
		}
		break;
	case TELNET_STATE_IAC:
		/* don't consume byte yet */
		switch (b) {
		case IAC:
			/* escaped IAC, return one IAC */
			telnet_state = TELNET_STATE_CONNECTED;
			return telnet.read();
		case WILL:
			/* server can do something */
			telnet_state = TELNET_STATE_IAC_WILL;
			break;
		case WONT:
			/* server will not do something */
			telnet_state = TELNET_STATE_IAC_WONT;
			break;
		case DO:
			/* server wants us to do something */
			telnet_state = TELNET_STATE_IAC_DO;
			break;
		case DONT:
			/* server wants us to not do something */
			telnet_state = TELNET_STATE_IAC_DONT;
			break;
		case SB:
			/* sub-negotiate */
			telnet_state = TELNET_STATE_IAC_SB;
			telnet_sb_len = 0;
			break;
		default:
			/* something else, return the original IAC */
			telnet_state = TELNET_STATE_CONNECTED;
			return IAC;
			/* this next non-IAC byte will get returned next */
		}
		/* consume byte */
		telnet.read();
		break;
	case TELNET_STATE_IAC_SB:
		/* keep reading until we see [^IAC] IAC SE */
		b = telnet.read();
		TELNET_IAC_DEBUG("telnet_read: SB[%d] %s",
		    telnet_sb_len, telnet_iac_name(b));
		if (b == SE && telnet_sb_len > 0 &&
		    telnet_sb[telnet_sb_len - 1] == IAC) {
			TELNET_IAC_DEBUG("telnet_read: processing SB");
			if (telnet_sb[1] == SEND) {
				switch (telnet_sb[0]) {
				case IAC_TTYPE:
					telnet_send_ttype();
					break;
				case IAC_NAWS:
					telnet_send_naws();
					break;
				case IAC_TSPEED:
					telnet_send_tspeed();
					break;
				default:
					TELNET_IAC_DEBUG("unsupported IAC SB %s",
					    telnet_iac_name(telnet_sb[0]));
				}
			} else {
				syslog.logf(LOG_WARNING, "%s: server is "
				    "telling us SB %d?", __func__, telnet_sb[0]);
			}
			telnet_state = TELNET_STATE_CONNECTED;
		} else {
			if (telnet_sb_len < sizeof(telnet_sb))
				telnet_sb[telnet_sb_len++] = b;
			else {
				syslog.logf(LOG_ERR, "IAC SB overflow!");
				telnet_state = TELNET_STATE_CONNECTED;
				telnet_sb_len = 0;
			}
		}
		break;
	case TELNET_STATE_IAC_WILL:
		switch (b = telnet.read()) {
		case IAC_ECHO:
			TELNET_IAC_DEBUG("telnet_read: -> IAC DO ECHO");
			telnet.printf("%c%c%c", IAC, DO, b);
			break;
		case IAC_SGA:
			TELNET_IAC_DEBUG("telnet_read: -> IAC DO SGA");
			telnet.printf("%c%c%c", IAC, DO, b);
			break;
		case IAC_ENCRYPT:
			/* refuse with DONT to satisfy NetBSD's telnetd */
			TELNET_IAC_DEBUG("telnet_read: -> IAC DONT ENCRYPT");
			telnet.printf("%c%c%c", IAC, DONT, b);
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	case TELNET_STATE_IAC_WONT:
		switch (b = telnet.read()) {
		default:
			/* we don't care about any of these yet */
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	case TELNET_STATE_IAC_DO:
		b = telnet.read();
		switch (b) {
		case IAC_BINARY:
			TELNET_IAC_DEBUG("telnet_read: -> IAC WILL BINARY");
			telnet.printf("%c%c%c", IAC, WILL, b);
			break;
		case IAC_NAWS:
		case IAC_TSPEED:
		case IAC_TTYPE:
		case IAC_FLOWCTRL:
			break;
		case IAC_LINEMODE:
			/* refuse this, we want the server to handle input */
			/* FALLTHROUGH */
		default:
			TELNET_IAC_DEBUG("telnet_read: -> IAC WONT %s",
			    telnet_iac_name(b));
			telnet.printf("%c%c%c", IAC, WONT, b);
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	case TELNET_STATE_IAC_DONT:
		switch (b = telnet.read()) {
		default:
			TELNET_IAC_DEBUG("telnet_read: IAC DONT %s",
			    telnet_iac_name(b));
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	default:
		b = telnet.read();
		TELNET_IAC_DEBUG("telnet_read: read 0x%x but in state %d", b,
		    telnet_state);
		break;
	}

	return -1;
}

int
telnet_write(char b)
{
	/* escape */
	if (settings->telnet && b == IAC) {
		TELNET_DATA_DEBUG("telnet_write: escaped IAC");
		telnet.write(b);
	}

	TELNET_DATA_DEBUG("telnet_write: 0x%x", b);
	return telnet.write(b);
}

int
telnet_write(String s)
{
	String s2 = "";

	for (size_t i = 0; i < s.length(); i++) {
		/* escape */
		if (settings->telnet && s.charAt(i) == IAC) {
			TELNET_DATA_DEBUG("telnet_write: escaped IAC");
			s2 += IAC;
		}
		s2 += s.charAt(i);
		TELNET_DATA_DEBUG("telnet_write: 0x%x", s.charAt(i));
	}

	return telnet.print(s2);
}
