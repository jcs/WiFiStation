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
	TELNET_STATE_DISCONNECTED,
	TELNET_STATE_CONNECTED,
	TELNET_STATE_IAC,
	TELNET_STATE_IAC_SB,
	TELNET_STATE_IAC_WILL,
	TELNET_STATE_IAC_DO,
};

uint8_t telnet_state = TELNET_STATE_DISCONNECTED;
uint8_t telnet_sb[64];
uint8_t telnet_sb_len = 0;
bool telnet_echoing = true;

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

#ifdef TELNET_TRACE
#define TELNET_DEBUG(...) { outputf(__VA_ARGS__); }
#else
#define TELNET_DEBUG(...) {}
#endif

void telnet_process_sb(void);

int
telnet_connect(char *host, uint16_t port)
{
	if (telnet_state != TELNET_STATE_DISCONNECTED) {
		outputf("can't telnet_connect, telnet_state %d\r\n",
		    telnet_state);
		return 1;
	}

	if (!telnet.connect(host, port))
		return 1;

	telnet.setNoDelay(true);

	telnet_state = TELNET_STATE_CONNECTED;
	telnet_echoing = true;

	if (settings->telnet) {
		/* start by sending some options we support */
		TELNET_DEBUG("%s: -> IAC DO SUPPRESS GO AHEAD\r\n", __func__);
		telnet.printf("%c%c%c", IAC, DO, IAC_SGA);
		TELNET_DEBUG("%s: -> IAC WILL TTYPE\r\n", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_TTYPE);
		TELNET_DEBUG("%s: -> IAC WILL NAWS\r\n", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_NAWS);
		TELNET_DEBUG("%s: -> IAC WILL TSPEED\r\n", __func__);
		telnet.printf("%c%c%c", IAC, WILL, IAC_TSPEED);
		TELNET_DEBUG("%s: -> IAC DO STATUS\r\n", __func__);
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
		telnet.stop();
		telnet_state = TELNET_STATE_DISCONNECTED;
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

void
telnet_process_sb(void)
{
	switch (telnet_sb[0]) {
	case IAC_TTYPE:
		if (telnet_sb[1] != SEND) {
			TELNET_DEBUG("%s: server is telling us TYPE? ",
			    __func__);
			goto dump_sb;
		}

		TELNET_DEBUG("%s: -> IAC SB TTYPE %s\r\n", __func__,
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
		break;
	case IAC_NAWS:
		if (telnet_sb[1] != SEND) {
			TELNET_DEBUG("%s: server is telling us NAWS? ",
			    __func__);
			goto dump_sb;
		}

		TELNET_DEBUG("%s: -> IAC SB NAWS %dx%d\r\n", __func__,
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
		break;
	default:
		TELNET_DEBUG("handle IAC SB:");
		goto dump_sb;
	}

	return;
dump_sb:
	for (int i = 0; i < telnet_sb_len; i++)
		TELNET_DEBUG(" %02d", telnet_sb[i]);
	TELNET_DEBUG("\r\n");
}

#ifdef TELNET_TRACE
static char iac_name[16];
char *
telnet_iac_name(char iac)
{
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
	default:
		sprintf(iac_name, "%d", iac);
		break;
	}
	return iac_name;
}
#endif

int
telnet_read(void)
{
	char b;

	if (!telnet.available())
		return -1;

	b = telnet.read();

	if (telnet_state != TELNET_STATE_CONNECTED)
		TELNET_DEBUG("telnet_state[%d]: 0x%x (%d)\r\n", telnet_state,
		    b, b);

	/* when AT$NET=0, just pass everything as-is */
	if (!settings->telnet)
		return b;

	switch (telnet_state) {
	case TELNET_STATE_CONNECTED:
		switch (b) {
		case IAC:
			telnet_state = TELNET_STATE_IAC;
			break;
		default:
			return b;
		}
		break;
	case TELNET_STATE_IAC:
		switch (b) {
		case IAC:
			/* escaped IAC */
			return b;
		case WILL:
			/* server can do something */
			telnet_state = TELNET_STATE_IAC_WILL;
			break;
		case DO:
			/* server wants us to do something */
			telnet_state = TELNET_STATE_IAC_DO;
			break;
		case SB:
			/* sub-negotiate */
			telnet_state = TELNET_STATE_IAC_SB;
			telnet_sb_len = 0;
			break;
		default:
			telnet_state = TELNET_STATE_CONNECTED;
		}
		break;
	case TELNET_STATE_IAC_SB:
		/* keep reading until we see [^IAC] IAC SE */
		if (b == SE && telnet_sb_len > 0 &&
		    telnet_sb[telnet_sb_len - 1] == IAC) {
		    	telnet_process_sb();
			telnet_state = TELNET_STATE_CONNECTED;
		} else {
			if (telnet_sb_len < sizeof(telnet_sb))
				telnet_sb[telnet_sb_len++] = b;
			else {
				outputf("IAC SB overflow!\r\n");
				telnet_state = TELNET_STATE_CONNECTED;
			}
		}
		break;
	case TELNET_STATE_IAC_WILL: {
		TELNET_DEBUG("telnet_read: IAC WILL %s\r\n",
		    telnet_iac_name(b));
		switch (b) {
		case IAC_ECHO:
			TELNET_DEBUG("telnet_read: -> IAC DO ECHO\r\n");
			telnet.printf("%c%c%c", IAC, DO, b);
			break;
		case IAC_SGA:
			TELNET_DEBUG("telnet_read: -> IAC DO SGA\r\n");
			telnet.printf("%c%c%c", IAC, DO, b);
			break;
		default:
			TELNET_DEBUG("telnet_read: -> IAC DONT %s\r\n",
			    telnet_iac_name(b));
			telnet.printf("%c%c%c", IAC, DONT, b);
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	}
	case TELNET_STATE_IAC_DO:
		TELNET_DEBUG("telnet_read: IAC DO %s\r\n", telnet_iac_name(b));
		switch (b) {
		case IAC_BINARY:
			TELNET_DEBUG("telnet_read: -> IAC WILL %s\r\n",
			    telnet_iac_name(b));
			telnet.printf("%c%c%c", IAC, WILL, b);
			break;
		case IAC_TTYPE:
		case IAC_NAWS:
		case IAC_TSPEED:
			/* we support these but want them as SB */
			/* FALLTHROUGH */
		case IAC_LINEMODE:
			/* refuse this, we want the server to handle input */
			/* FALLTHROUGH */
		default:
			TELNET_DEBUG("telnet_read: -> IAC WONT %s\r\n",
			    telnet_iac_name(b));
			telnet.printf("%c%c%c", IAC, WONT, b);
			break;
		}
		telnet_state = TELNET_STATE_CONNECTED;
		break;
	default:
		TELNET_DEBUG("telnet_read: read 0x%x but in state %d\r\n", b,
		    telnet_state);
		break;
	}

	return -1;
}

int
telnet_write(char b)
{
	/* escape */
	if (settings->telnet && b == IAC)
		telnet.write(b);

	return telnet.write(b);
}

int
telnet_write(String s)
{
	String s2 = "";

	for (size_t i = 0; i < s.length(); i++) {
		/* escape */
		if (settings->telnet && s.charAt(i) == IAC)
			s2 += IAC;
		s2 += s.charAt(i);
	}

	return telnet.print(s2);
}
