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
#include "MCP23S18.h"

enum {
	STATE_AT,
	STATE_TELNET,
};

static char curcmd[128] = { 0 };
static char lastcmd[128] = { 0 };
static unsigned int curcmdlen = 0;
static unsigned int lastcmdlen = 0;
static uint8_t state = STATE_AT;

void
loop(void)
{
	int b = -1;
	long now = millis();

	http_process();

	switch (state) {
	case STATE_AT:
		if ((b = ms_read()) != -1) {
			if (!mailstation_alive) {
				/* mailstation can only come alive sending 'a' */
				if (b != 'A' && b != 'a')
					return;
				mailstation_alive = true;
			}
		} else if (Serial.available() && (b = Serial.read()))
			serial_alive = true;
		else
			return;

		/* USR modem mode, ignore input not starting with at or a/ */
		if (curcmdlen == 0 && (b != 'A' && b != 'a')) {
			return;
		} else if (curcmdlen == 1 && b == '/') {
			output("/\r\n");
			curcmd[0] = '\0';
			curcmdlen = 0;
			exec_cmd((char *)&lastcmd, lastcmdlen);
			break;
		} else if (curcmdlen == 1 && (b != 'T' && b != 't')) {
			output("\b \b");
			curcmdlen = 0;
			return;
		}

		switch (b) {
		case '\n':
		case '\r':
			if (b == '\r') {
				/* if sender is using \r\n, ignore the \n */
				now = millis();
				while (millis() - now < 10) {
					int b2 = Serial.peek();
					if (b2 == -1)
						continue;
					else if (b2 == '\n') {
						/* this is a \r\n, ignore \n */
						Serial.read();
						break;
					} else {
						/* some other data */
						break;
					}
				}
			}
			output("\r\n");
			curcmd[curcmdlen] = '\0';
			exec_cmd((char *)&curcmd, curcmdlen);
			curcmd[0] = '\0';
			curcmdlen = 0;
			break;
		case '\b':
		case 127:
			if (curcmdlen) {
				output("\b \b");
				curcmdlen--;
			}
			break;
		default:
			curcmd[curcmdlen++] = b;
			output(b);
		}
		break;
	case STATE_TELNET:
		if (mailstation_alive && (b = ms_read()) != -1) {
			mailstation_alive = true;
			if (b == '\e') {
				/* probably a multi-character command */
				String seq = "" + (char)b;
				unsigned long t = millis();

				while (millis() - t < 50) {
					if ((b = ms_read()) != -1)
						seq += (char)b;
				}
				telnet_write(seq);
			} else
				telnet_write(b);
		} else if (Serial.available() && (b = Serial.read())) {
			serial_alive = true;
			telnet_write(b);
		} else if ((b = telnet_read()) != -1) {
			if (mailstation_alive)
				ms_write(b);
			if (serial_alive)
				Serial.write(b);
			return;
		} else if (!telnet_connected()) {
			outputf("\r\nNO CARRIER\r\n");
			state = STATE_AT;
			break;
		}
		break;
	}
}

void
exec_cmd(char *cmd, size_t len)
{
	unsigned long t;
	char *errstr = NULL;

	char *lcmd = (char *)malloc(len + 1);
	if (lcmd == NULL) {
		outputf("ERROR malloc %zu failed\r\n", len);
		return;
	}

	for (size_t i = 0; i < len; i++)
		lcmd[i] = tolower(cmd[i]);
	lcmd[len] = '\0';

	if (len < 2 || lcmd[0] != 'a' || lcmd[1] != 't') {
		errstr = strdup("not an AT command");
		goto error;
	}

	memcpy(&lastcmd, lcmd, len + 1);
	lastcmdlen = len;

	if (len == 2) {
		output("OK\r\n");
		return;
	}

	switch (lcmd[2]) {
	case 'd': {
		char *host, *ohost;
		uint16_t port;
		int chars;

		/* ATDT: dial a host */
		if (len < 5 || lcmd[3] != 't')
			goto error;

		host = ohost = (char *)malloc(len);
		if (host == NULL)
			goto error;
		host[0] = '\0';

		if (sscanf(lcmd, "atdt%[^:]:%d%n", host, &port, &chars) == 2 &&
		    chars > 0)
			/* matched host:port */
			;
		else if (sscanf(lcmd, "atdt%[^:]%n", host, &chars) == 1 &&
		    chars > 0)
		    	/* host without port */
			port = 23;
		else {
			errstr = strdup("invalid hostname");
			goto error;
		}

		while (host[0] == ' ')
			host++;

		if (host[0] == '\0') {
			errstr = strdup("blank hostname");
			goto error;
		}

		outputf("DIALING %s:%d\r\n", host, port);

		if (telnet_connect(host, port) == 0) {
			outputf("CONNECT %d %s:%d\r\n", settings->baud, host,
			    port);
			state = STATE_TELNET;
		} else {
			outputf("NO ANSWER\r\n");
		}

		free(ohost);

		break;
	}
	case 'i':
		if (len > 4)
			goto error;

		switch (len == 3 ? '0' : cmd[3]) {
		case '0':
			/* ATI or ATI0: show settings */
			outputf("Firmware version:  %s\r\n",
			    WIFISTATION_VERSION);
			outputf("Serial baud rate:  %d\r\n",
			    settings->baud);
			outputf("Default WiFi SSID: %s\r\n",
			    settings->wifi_ssid);
			outputf("Current WiFi SSID: %s\r\n", WiFi.SSID());
			outputf("WiFi Connected:    %s\r\n",
			    WiFi.status() == WL_CONNECTED ? "yes" : "no");
			if (WiFi.status() == WL_CONNECTED) {
				outputf("IP Address:        %s\r\n",
				    WiFi.localIP().toString().c_str());
				outputf("Gateway IP:        %s\r\n",
				    WiFi.gatewayIP().toString().c_str());
				outputf("DNS Server IP:     %s\r\n",
				    WiFi.dnsIP().toString().c_str());
			}
			outputf("HTTP Server:       %s\r\n",
			    settings->http_server ? "yes" : "no");
			output("OK\r\n");
			break;
		case '1': {
			/* ATI1: scan for wifi networks */
			int n = WiFi.scanNetworks();

			for (int i = 0; i < n; i++) {
				outputf("%02d: %s (chan %d, %ddBm, ",
				    i + 1,
				    WiFi.SSID(i).c_str(),
				    WiFi.channel(i),
				    WiFi.RSSI(i));

				switch (WiFi.encryptionType(i)) {
				case ENC_TYPE_WEP:
					output("WEP");
					break;
				case ENC_TYPE_TKIP:
					output("WPA-PSK");
					break;
				case ENC_TYPE_CCMP:
					output("WPA2-PSK");
					break;
				case ENC_TYPE_NONE:
					output("NONE");
					break;
				case ENC_TYPE_AUTO:
					output("WPA-PSK/WPA2-PSK");
					break;
				default:
					outputf("?(%d)",
					    WiFi.encryptionType(i));
				}

				output(")\r\n");
			}
			output("OK\r\n");
			break;
		}
		case '3':
			/* ATI3: show version */
			outputf("%s\r\nOK\r\n", WIFISTATION_VERSION);
			break;
		default:
			goto error;
		}
		break;
	case 'z':
		output("OK\r\n");
		ESP.restart();
		break;
	case '$':
		/* wifi232 commands */

		if (strcmp(lcmd, "at$http=0") == 0) {
			/* AT$HTTP=0: disable http server */
			settings->http_server = 0;
			http_setup();
			output("OK\r\n");
		} else if (strcmp(lcmd, "at$http=1") == 0) {
			/* AT$HTTP=1: enable http server */
			settings->http_server = 1;
			http_setup();
			output("OK\r\n");
		} else if (strcmp(lcmd, "at$http?") == 0) {
			/* AT$HTTP?: show http server setting */
			outputf("%d\r\nOK\r\n", settings->http_server);
		} else if (strcmp(lcmd, "at$net=0") == 0) {
			/* AT$NET=0: disable telnet setting */
			settings->telnet = 0;
			output("OK\r\n");
		} else if (strcmp(lcmd, "at$net=1") == 0) {
			/* AT$NET=1: enable telnet setting */
			settings->telnet = 1;
			output("OK\r\n");
		} else if (strcmp(lcmd, "at$net?") == 0) {
			/* AT$NET?: show telnet setting */
			outputf("%d\r\nOK\r\n", settings->telnet);
		} else if (strncmp(lcmd, "at$pass=", 8) == 0) {
			/* AT$PASS=...: store wep/wpa passphrase */
			memset(settings->wifi_pass, 0,
			    sizeof(settings->wifi_pass));
			strncpy(settings->wifi_pass, cmd + 8,
			    sizeof(settings->wifi_pass));
			output("OK\r\n");

			WiFi.disconnect();
			if (settings->wifi_ssid[0])
				WiFi.begin(settings->wifi_ssid,
				    settings->wifi_pass);
		} else if (strcmp(lcmd, "at$pass?") == 0) {
			/* AT$PASS?: print wep/wpa passphrase */
			outputf("%s\r\nOK\r\n", settings->wifi_pass);
		} else if (strcmp(lcmd, "at$pins?") == 0) {
			/* AT$PINS?: watch MCP23S18 lines for debugging */
			uint16_t prev = UINT16_MAX;
			int i, done = 0;
			unsigned char b, bit, n, data = 0;
			extern MCP23S18 mcp;

			ms_datadir(INPUT);

			while (!done) {
				ESP.wdtFeed();

				/* watch for ^C */
				if (Serial.available()) {
					switch (b = Serial.read()) {
					case 3:
						/* ^C */
						done = 1;
					case 'd':
						Serial.printf("data input\r\n");
						ms_datadir(INPUT);
						break;
					case 'D':
						Serial.printf("data output\r\n");
						ms_datadir(OUTPUT);
						break;
					case 'L':
						Serial.printf("linefeed high\r\n");
						mcp.digitalWrite(pLineFeed, HIGH);
						break;
					case 'l':
						Serial.printf("linefeed low\r\n");
						mcp.digitalWrite(pLineFeed, LOW);
						break;
					case 'S':
						Serial.printf("strobe high\r\n");
						mcp.digitalWrite(pStrobe, HIGH);
						break;
					case 's':
						Serial.printf("strobe low\r\n");
						mcp.digitalWrite(pStrobe, LOW);
						break;
					case '0':
					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
						n = (b - '0');
						bit = (data & (1 << n));
						if (bit)
							data &= ~(1 << n);
						else
							data |= (1 << n);
						Serial.printf("turning data%d "
						    "%s (0x%x)\r\n",
						    n, bit ? "off" : " on",
						    data);
						ms_datadir(OUTPUT);
						ms_writedata(data);
						break;
					}
				}

				uint16_t all = ms_status();
				if (all != prev) {
					Serial.print("data: ");
					for (i = 0; i < 8; i++)
						Serial.print((all & (1 << i)) ?
						    '1' : '0');

					Serial.print(" status: ");
					for (; i < 16; i++)
						Serial.print((all & (1 << i)) ?
						    '1' : '0');
					Serial.print("\r\n");
					prev = all;
				}
			}
			ms_datadir(INPUT);
			Serial.print("OK\r\n");
		} else if (strncmp(lcmd, "at$sb=", 6) == 0) {
			uint32_t baud = 0;
			int chars = 0;

			/* AT$SB=...: set baud rate */
			if (sscanf(lcmd, "at$sb=%d%n", &baud, &chars) != 1 ||
		    	    chars == 0) {
				output("ERROR invalid baud rate\r\n");
				break;
			}

			switch (baud) {
			case 110:
			case 300:
			case 1200:
			case 2400:
			case 4800:
			case 9600:
			case 19200:
			case 38400:
			case 57600:
			case 115200:
				settings->baud = baud;
				outputf("OK switching to %d\r\n",
				    settings->baud);
				Serial.flush();
				Serial.begin(settings->baud);
				break;
			default:
				output("ERROR unsupported baud rate\r\n");
				break;
			}
		} else if (strcmp(lcmd, "at$sb?") == 0) {
			/* AT$SB?: print baud rate */
			outputf("%d\r\nOK\r\n", settings->baud);
		} else if (strncmp(lcmd, "at$ssid=", 8) == 0) {
			/* AT$SSID=...: set wifi ssid */
			memset(settings->wifi_ssid, 0,
			    sizeof(settings->wifi_ssid));
			strncpy(settings->wifi_ssid, cmd + 8,
			    sizeof(settings->wifi_ssid));
			output("OK\r\n");

			WiFi.disconnect();
			if (settings->wifi_ssid[0])
				WiFi.begin(settings->wifi_ssid,
				    settings->wifi_pass);
		} else if (strcmp(lcmd, "at$ssid?") == 0) {
			/* AT$SSID?: print wifi ssid */
			outputf("%s\r\nOK\r\n", settings->wifi_ssid);
		} else if (strncmp(lcmd, "at$tts=", 7) == 0) {
			/* AT$TTS=: set telnet NAWS */
			int w, h, chars;
			if (sscanf(lcmd + 7, "%dx%d%n", &w, &h, &chars) == 2 &&
			    chars > 0) {
				if (w < 1 || w > 255) {
					errstr = strdup("invalid width");
					goto error;
				}
				if (h < 1 || h > 255) {
					errstr = strdup("invalid height");
					goto error;
				}

				settings->telnet_tts_w = w;
				settings->telnet_tts_h = h;
				output("OK\r\n");
			} else {
				errstr = strdup("must be WxH");
				goto error;
			}
		} else if (strcmp(lcmd, "at$tts?") == 0) {
			/* AT$TTS?: show telnet NAWS setting */
			outputf("%dx%d\r\nOK\r\n", settings->telnet_tts_w,
			    settings->telnet_tts_h);
		} else if (strncmp(lcmd, "at$tty=", 7) == 0) {
			/* AT$TTY=: set telnet TTYPE */
			memset(settings->telnet_tterm, 0,
			    sizeof(settings->telnet_tterm));
			strncpy(settings->telnet_tterm, cmd + 7,
			    sizeof(settings->telnet_tterm));
			output("OK\r\n");
		} else if (strcmp(lcmd, "at$tty?") == 0) {
			/* AT$TTY?: show telnet TTYPE setting */
			outputf("%s\r\nOK\r\n", settings->telnet_tterm);
		} else if (strncmp(lcmd, "at$upload", 9) == 0) {
			/* AT$UPLOAD: mailstation program loader */
			unsigned int bytes = 0;
			unsigned char b;

			if (sscanf(lcmd, "at$upload%u", &bytes) != 1 ||
			    bytes < 1)
				goto error;

			if (bytes > MAX_UPLOAD_SIZE) {
				outputf("ERROR size cannot be larger than "
				    "%d\r\n", MAX_UPLOAD_SIZE);
				break;
			}

			/*
			 * Assume it's now dead until we see it on the other
			 * side of the upload
			 */
			mailstation_alive = false;

			/* send low and high bytes of size */
			if (ms_write(bytes & 0xff) != 0 ||
			    ms_write((bytes >> 8) & 0xff) != 0) {
				output("ERROR MailStation failed to receive "
				    "size\r\n");
				break;
			}

			outputf("OK send your %d byte%s\r\n", bytes,
			    bytes == 1 ? "" : "s");

			t = millis();
			int written = 0;
			char cksum = 0;
			while (bytes > 0) {
				if (!Serial.available()) {
					if (millis() - t > 5000)
						break;
					yield();
					continue;
				}

				b = Serial.read();

				if (ms_write(b) != 0)
					break;

				cksum ^= b;

				if (++written % 32 == 0)
					output(cksum);

				bytes--;
				t = millis();
			}

			if (bytes == 0) {
				output(cksum);
				output("\r\nOK good luck\r\n");
			} else
				outputf("\r\nERROR MailStation failed to "
				    "receive byte with %d byte%s left\r\n",
				    bytes, (bytes == 1 ? "" : "s"));
			break;
		} else
			goto error;
		break;
	case '&':
		if (len < 4)
			goto error;

		switch (lcmd[3]) {
		case 'w':
			if (len != 4)
				goto error;

			/* AT&W: save settings */
			if (!EEPROM.commit())
				goto error;

			output("OK\r\n");
			break;
		default:
			goto error;
		}
		break;
	default:
		goto error;
	}

	if (lcmd)
		free(lcmd);
	return;

error:
	if (lcmd)
		free(lcmd);

	output("ERROR");
	if (errstr != NULL) {
		outputf(" %s", errstr);
		free(errstr);
	}
	output("\r\n");
}
