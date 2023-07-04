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

/*
 * Useful AT command sets to emulate:
 *
 * USRobotics Courier 56K Business Modem
 * http://web.archive.org/web/20161116174421/http://support.usr.com/support/3453c/3453c-ug/alphabetic.html
 *
 * Xecom XE3314L
 * http://web.archive.org/web/20210816224031/http://static6.arrow.com/aropdfconversion/63e466a4e0c7e004c40f79e4dbe4c1356a3dcef6/xe3314l.pdf
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
static int plusses = 0;
static unsigned long plus_wait = 0;
static bool wifi_ever_connected = false;

void
loop(void)
{
	int b = -1, i;
	long now = millis();

	if (!wifi_ever_connected && WiFi.status() == WL_CONNECTED) {
		rst_info *resetInfo = ESP.getResetInfoPtr();
		syslog.logf(LOG_INFO, "connected, reset from %s (%d)",
		    ESP.getResetReason().c_str(),
		    (resetInfo == nullptr ? 0 : (*resetInfo).reason));
		wifi_ever_connected = true;
	}

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
			if (settings->echo)
				output("/\r");
			curcmd[0] = '\0';
			curcmdlen = 0;
			exec_cmd((char *)&lastcmd, lastcmdlen);
			break;
		} else if (curcmdlen == 1 && (b != 'T' && b != 't')) {
			if (settings->echo)
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
			output("\r");
			curcmd[curcmdlen] = '\0';
			exec_cmd((char *)&curcmd, curcmdlen);
			curcmd[0] = '\0';
			curcmdlen = 0;
			break;
		case '\b':
		case 127:
			if (curcmdlen) {
				if (settings->echo)
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
		b = -1;

		if (mailstation_alive && (b = ms_read()) != -1) {
			if (b == '\e') {
				/* probably a multi-character command */
				String seq = String((char)b);
				unsigned long t = millis();

				while (millis() - t < 50) {
					if ((b = ms_read()) != -1)
						seq += (char)b;
				}
				telnet_write(seq);
				plusses = 0;
				break;
			}
		} else if (Serial.available() && (b = Serial.read()))
			serial_alive = true;

		if (b == -1 && plus_wait > 0 && (millis() - plus_wait) >= 500) {
			/* received no input within 500ms of a plus */
			if (plusses >= 3) {
				state = STATE_AT;
				if (!settings->quiet) {
					if (settings->verbal)
						output("\r\nOK\r\n");
					else
						output("0\r");
				}
			} else {
				/* cancel, flush any plus signs received */
				for (i = 0; i < plusses; i++)
					telnet_write("+");
			}
			plusses = 0;
			plus_wait = 0;
		} else if (b != -1) {
			if (b == '+') {
				plusses++;
				plus_wait = millis();
				break;
			}

			if (plusses) {
				for (i = 0; i < plusses; i++)
					telnet_write("+");
				plusses = 0;
			}
			plus_wait = 0;
			telnet_write(b);
			break;
		}

		if ((b = telnet_read()) != -1) {
			if (mailstation_alive)
				ms_write(b);
			if (serial_alive)
				Serial.write(b);
			return;
		} else if (!telnet_connected()) {
			if (!settings->quiet) {
				if (settings->verbal)
					output("\r\nNO CARRIER\r\n");
				else
					output("3\r");
			}
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
	char *lcmd, *olcmd;
	char cmd_char;
	uint8_t cmd_num = 0;
	bool did_nl = false;
	bool did_response = false;

	lcmd = olcmd = (char *)malloc(len + 1);
	if (lcmd == NULL) {
		if (settings->verbal)
			outputf("ERROR malloc %zu failed\r\n", len);
		else
			output("4\r");
		return;
	}

#ifdef AT_TRACE
	syslog.logf(LOG_DEBUG, "%s: parsing \"%s\"", __func__, cmd);
#endif

	for (size_t i = 0; i < len; i++)
		lcmd[i] = tolower(cmd[i]);
	lcmd[len] = '\0';

	/* shouldn't be able to get here, but just in case */
	if (len < 2 || lcmd[0] != 'a' || lcmd[1] != 't') {
		errstr = strdup("not an AT command");
		goto error;
	}

	memcpy(&lastcmd, lcmd, len + 1);
	lastcmdlen = len;

	/* strip AT */
	cmd += 2;
	lcmd += 2;
	len -= 2;

parse_cmd:
	if (lcmd[0] == '\0')
		goto done_parsing;

	/* remove command character */
	cmd_char = lcmd[0];
	len--;
	cmd++;
	lcmd++;

	/* find optional single digit after command, defaulting to 0 */
	cmd_num = 0;
	if (cmd[0] >= '0' && cmd[0] <= '9') {
		if (cmd[1] >= '0' && cmd[1] <= '9')
			/* nothing uses more than 1 digit */
			goto error;
		cmd_num = cmd[0] - '0';
		len--;
		cmd++;
		lcmd++;
	}

#ifdef AT_TRACE
	syslog.logf(LOG_DEBUG, "%s: parsing AT %c[%d] args \"%s\"", __func__,
	    cmd_char, cmd_num, lcmd);
#endif

	switch (cmd_char) {
	case 'd': {
		char *host, *ohost, *bookmark;
		uint16_t port;
		int chars;
		int index;

		if (len < 2)
			goto error;

		switch (lcmd[0]) {
		case 't':
			/* ATDT: dial a host */
			host = ohost = (char *)malloc(len);
			if (host == NULL)
				goto error;
			host[0] = '\0';
			if (sscanf(lcmd, "t%[^:]:%u%n", host, &port,
			    &chars) == 2 && chars > 0)
				/* matched host:port */
				;
			else if (sscanf(lcmd, "t%s %u%n", host, &port,
			    &chars) == 2 && chars > 0)
				/* matched host port */
				;
			else if (sscanf(lcmd, "t%[^:]%n", host, &chars) == 1
			    && chars > 0)
				/* host without port */
				port = 23;
			else {
				errstr = strdup("invalid hostname");
				goto error;
			}
			break;
		case 's':
			/* ATDS: dial a stored host */
			if (sscanf(lcmd, "s%d", &index) != 1)
				goto error;

			if (index < 1 || index > NUM_BOOKMARKS) {
				errstr = strdup("invalid index");
				goto error;
			}

			bookmark = settings->bookmarks[index - 1];

			host = ohost = (char *)malloc(BOOKMARK_SIZE);
			if (host == NULL)
				goto error;

			host[0] = '\0';

			if (sscanf(bookmark, "%[^:]:%hu%n", host, &port,
			    &chars) == 2 && chars > 0)
				/* matched host:port */
				;
			else if (sscanf(bookmark, "%[^:]%n", host, &chars) == 1
			    && chars > 0)
				/* host without port */
				port = 23;
			else {
				errstr = strdup("invalid hostname");
				goto error;
			}
			break;
		default:
			goto error;
		}

		/* no commands can follow */
		len = 0;

		/* skip leading spaces */
		while (host[0] == ' ')
			host++;

		if (host[0] == '\0') {
			errstr = strdup("blank hostname");
			goto error;
		}

		if (!settings->quiet && settings->verbal)
			outputf("\nDIALING %s:%d\r\n", host, port);

		if (telnet_connect(host, port) == 0) {
			if (!settings->quiet) {
				if (settings->verbal)
					outputf("CONNECT %d %s:%d\r\n",
					    settings->baud, host, port);
				else
					output("18\r"); /* 57600 */
			}
			state = STATE_TELNET;
		} else if (!settings->quiet) {
			if (settings->verbal)
				output("\nNO ANSWER\r\n");
			else
				output("8\r");
		}

		did_nl = true;
		free(ohost);
		break;
	}
	case 'e':
		/* ATE/ATE0 or ATE1: disable or enable echo */
		switch (cmd_num) {
		case 0:
			settings->echo = 0;
			break;
		case 1:
			settings->echo = 1;
			break;
		default:
			goto error;
		}
		break;
	case 'h':
		/* ATH/ATH0: hangup */
		switch (cmd_num) {
		case 0:
			telnet_disconnect();
			break;
		default:
			goto error;
		}
		break;
	case 'i':
		/* ATI/ATI#: show information pages */
		switch (cmd_num) {
		case 0: {
			/* ATI0: show settings */
			ip4_addr_t t_addr;
			output("\n");

			outputf("Firmware version:  %s\r\n",
			    WIFISTATION_VERSION);
			outputf("Default baud rate: %d\r\n", settings->baud);
			outputf("Current baud rate: %d\r\n",
			    Serial.baudRate());
			outputf("Default WiFi SSID: %s\r\n",
			    settings->wifi_ssid);
			outputf("Current WiFi SSID: %s\r\n", WiFi.SSID());
			outputf("WiFi connected:    %s\r\n",
			    WiFi.status() == WL_CONNECTED ? "yes" : "no");
			if (WiFi.status() == WL_CONNECTED) {
				outputf("IP address:        %s\r\n",
				    WiFi.localIP().toString().c_str());
				outputf("Gateway IP:        %s\r\n",
				    WiFi.gatewayIP().toString().c_str());
				outputf("DNS server IP:     %s\r\n",
				    WiFi.dnsIP().toString().c_str());
			}
			outputf("MailStation alive: %s\r\n",
			    mailstation_alive ? "yes" : "no");
			outputf("HTTP server:       %s\r\n",
			    settings->http_server ? "yes" : "no");
			outputf("Syslog server:     %s\r\n",
			    settings->syslog_server);
			for (int i = 0; i < NUM_BOOKMARKS; i++) {
				if (settings->bookmarks[i][0] != '\0')
					outputf("ATDS bookmark %d:   %s\r\n",
					    i + 1, settings->bookmarks[i]);
			}
			did_nl = true;
			break;
		}
		default:
			goto error;
		}
		break;
	case 'o':
		/* ATO: go back online after a +++ */
		switch (cmd_num) {
		case 0:
			if (telnet_connected())
				state = STATE_TELNET;
			else
				goto error;
			break;
		default:
			goto error;
		}
		break;
	case 'q':
		/* ATQ/ATQ0 or ATQ1: enable or disable quiet */
		switch (cmd_num) {
		case 0:
			settings->quiet = 0;
			break;
		case 1:
		case 2:
			settings->quiet = 1;
			break;
		default:
			goto error;
		}
		break;
	case 'v':
		/* ATV/ATV0 or ATV1: enable or disable verbal responses */
		switch (cmd_num) {
		case 0:
			settings->verbal = 0;
			break;
		case 1:
			settings->verbal = 1;
			break;
		default:
			goto error;
		}
		break;
	case 'x':
		/* ATX/ATX#: ignore dialtone, certain results (not used) */
		break;
	case 'z':
		/* ATZ/ATZ0: restart */
		switch (cmd_num) {
		case 0:
			if (!settings->quiet) {
				if (settings->verbal)
					output("\nOK\r\n");
				else
					output("0\r");
			}
			ESP.restart();
			/* NOTREACHED */
		default:
			goto error;
		}
		break;
	case '$':
		/* wifi232 commands, all consume the rest of the input string */
		if (strcmp(lcmd, "http=0") == 0) {
			/* AT$HTTP=0: disable http server */
			settings->http_server = 0;
			http_setup();
		} else if (strcmp(lcmd, "http=1") == 0) {
			/* AT$HTTP=1: enable http server */
			settings->http_server = 1;
			http_setup();
		} else if (strcmp(lcmd, "http?") == 0) {
			/* AT$HTTP?: show http server setting */
			outputf("\n%d\r\n", settings->http_server);
		} else if (strcmp(lcmd, "net=0") == 0) {
			/* AT$NET=0: disable telnet setting */
			settings->telnet = 0;
		} else if (strcmp(lcmd, "net=1") == 0) {
			/* AT$NET=1: enable telnet setting */
			settings->telnet = 1;
		} else if (strcmp(lcmd, "net?") == 0) {
			/* AT$NET?: show telnet setting */
			outputf("\n%d\r\n", settings->telnet);
		} else if (strncmp(lcmd, "pass=", 5) == 0) {
			/* AT$PASS=...: store wep/wpa passphrase */
			memset(settings->wifi_pass, 0,
			    sizeof(settings->wifi_pass));
			strncpy(settings->wifi_pass, cmd + 5,
			    sizeof(settings->wifi_pass));

			WiFi.disconnect();
			if (settings->wifi_ssid[0])
				WiFi.begin(settings->wifi_ssid,
				    settings->wifi_pass);
		} else if (strcmp(lcmd, "pass?") == 0) {
			/* AT$PASS?: print wep/wpa passphrase */
			outputf("\n%s\r\n", settings->wifi_pass);
		} else if (strcmp(lcmd, "pins?") == 0) {
			/* AT$PINS?: watch MCP23S18 lines for debugging */
			uint16_t prev = UINT16_MAX;
			int i, done = 0;
			unsigned char b, bit, n, data = 0;
			extern MCP23S18 mcp;

			ms_datadir(INPUT);

			outputf("\n");

			while (!done) {
				ESP.wdtFeed();

				/* watch for ^C */
				if (Serial.available()) {
					switch (b = Serial.read()) {
					case 3:
						/* ^C */
						done = 1;
						break;
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
			did_nl = true;
		} else if (strncmp(lcmd, "sb=", 3) == 0) {
			uint32_t baud = 0;
			int chars = 0;

			/* AT$SB=...: set baud rate */
			if (sscanf(lcmd, "sb=%d%n", &baud, &chars) != 1 ||
		    	    chars == 0) {
				if (settings->verbal)
					output("ERROR invalid baud rate\r\n");
				else
					output("4\r");
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
				if (!settings->quiet) {
					if (settings->verbal)
						outputf("\nOK switching to "
						    "%d\r\n", settings->baud);
					else
						output("0\r");
				}
				Serial.flush();
				Serial.begin(settings->baud);
				break;
			default:
				output("ERROR unsupported baud rate\r\n");
				break;
			}
		} else if (strcmp(lcmd, "sb?") == 0) {
			/* AT$SB?: print baud rate */
			outputf("\n%d\r\n", settings->baud);
			did_nl = true;
		} else if (strcmp(lcmd, "scan") == 0) {
			/* AT$SCAN: scan for wifi networks */
			int n = WiFi.scanNetworks();

			/* don't scroll off the screen */
			if (n > 14)
				n = 14;

			output("\n");

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
			did_nl = true;
		} else if (strncmp(lcmd, "ssid=", 5) == 0) {
			/* AT$SSID=...: set wifi ssid */
			memset(settings->wifi_ssid, 0,
			    sizeof(settings->wifi_ssid));
			strncpy(settings->wifi_ssid, cmd + 5,
			    sizeof(settings->wifi_ssid));

			WiFi.disconnect();
			if (settings->wifi_ssid[0])
				WiFi.begin(settings->wifi_ssid,
				    settings->wifi_pass);
		} else if (strcmp(lcmd, "ssid?") == 0) {
			/* AT$SSID?: print wifi ssid */
			outputf("\n%s\r\n", settings->wifi_ssid);
			did_nl = true;
		} else if (strncmp(lcmd, "syslog=", 7) == 0) {
			/* AT$SYSLOG=...: set syslog server */
			memset(settings->syslog_server, 0,
			    sizeof(settings->syslog_server));
			strncpy(settings->syslog_server, cmd + 7,
			    sizeof(settings->syslog_server));
			syslog_setup();
			syslog.logf(LOG_INFO, "syslog server changed to %s",
			    settings->syslog_server);
		} else if (strcmp(lcmd, "syslog?") == 0) {
			/* AT$SYSLOG?: print syslog server */
			outputf("\n%s\r\n", settings->syslog_server);
			did_nl = true;
		} else if (strncmp(lcmd, "tts=", 4) == 0) {
			/* AT$TTS=: set telnet NAWS */
			int w, h, chars;
			if (sscanf(lcmd + 4, "%dx%d%n", &w, &h, &chars) == 2 &&
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
			} else {
				errstr = strdup("must be WxH");
				goto error;
			}
		} else if (strcmp(lcmd, "tts?") == 0) {
			/* AT$TTS?: show telnet NAWS setting */
			outputf("\n%dx%d\r\n", settings->telnet_tts_w,
			    settings->telnet_tts_h);
		} else if (strncmp(lcmd, "tty=", 4) == 0) {
			/* AT$TTY=: set telnet TTYPE */
			memset(settings->telnet_tterm, 0,
			    sizeof(settings->telnet_tterm));
			strncpy(settings->telnet_tterm, cmd + 4,
			    sizeof(settings->telnet_tterm));
		} else if (strcmp(lcmd, "at$tty?") == 0) {
			/* AT$TTY?: show telnet TTYPE setting */
			outputf("%s\r\nOK\r\n", settings->telnet_tterm);
		} else if (strncmp(lcmd, "update?", 7) == 0) {
			/* AT$UPDATE?: show whether an OTA update is available */
			char *url = NULL;
			if (strncmp(lcmd, "update? http", 12) == 0)
				url = lcmd + 8;
			update_process(url, false, false);
			did_response = true;
		} else if (strncmp(lcmd, "update!", 7) == 0) {
			/* AT$UPDATE!: force an OTA update */
			char *url = NULL;
			if (strncmp(lcmd, "update! http", 12) == 0)
				url = lcmd + 8;
			update_process(url, true, true);
			did_response = true;
		} else if (strcmp(lcmd, "update") == 0 ||
		    strncmp(lcmd, "update http", 11) == 0) {
			/* AT$UPDATE: do an OTA update */
			char *url = NULL;
			if (strncmp(lcmd, "update http", 11) == 0)
				url = lcmd + 7;
			update_process(url, true, false);
			did_response = true;
		} else if (strncmp(lcmd, "upload", 6) == 0) {
			/* AT$UPLOAD: mailstation program loader */
			unsigned int bytes = 0;
			unsigned char b;

			if (sscanf(lcmd, "at$upload%u", &bytes) != 1 ||
			    bytes < 1)
				goto error;

			if (bytes > (MAX_UPLOAD_SIZE - 1)) {
				outputf("\nERROR size cannot be larger than "
				    "%d\r\n", (MAX_UPLOAD_SIZE - 1));
				break;
			}

			/*
			 * Prevent output() from sending data to the
			 * MailStation until we see it on the other side of the
			 * upload.
			 */
			mailstation_alive = false;

			/*
			 * Send low and high bytes of size.
			 *
			 * XXX: Tell the MailStation we're sending one more
			 * byte than we're receiving from sendload so we can
			 * include one trailing null byte because sometimes the
			 * final ack of ms_write will fail to see ack line go
			 * low before WSLoader jumps to the payload.
			 * Figure out why that happens and remove this hack.
			 */
			if (ms_write((bytes + 1) & 0xff) != 0 ||
			    ms_write(((bytes + 1) >> 8) & 0xff) != 0) {
				output("ERROR MailStation failed to receive "
				    "size\r\n");
				break;
			}

			outputf("\nOK send your %d byte%s\r\n", bytes,
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
				t = millis();

				if (ms_write(b) != 0)
					break;

				cksum ^= b;
				written++;
				bytes--;

				if (written % 32 == 0)
					output(cksum);
			}

			if (bytes == 0) {
				output(cksum);
				output("\r\nOK good luck\r\n");
				/* XXX: trailing dummy byte, ignore response */
				ms_write(0);
			} else
				outputf("\r\nERROR MailStation failed to "
				    "receive byte with %d byte%s left\r\n",
				    bytes, (bytes == 1 ? "" : "s"));

			did_nl = true;
			break;
		} else
			goto error;

		/* consume all chars */
		len = 0;
		break;
	case '&':
		if (cmd[0] == '\0')
			goto error;

		cmd_char = lcmd[0];
		len--;
		cmd++;
		lcmd++;

		/* find optional single digit after &command, defaulting to 0 */
		cmd_num = 0;
		if (cmd[0] >= '0' && cmd[0] <= '9') {
			if (cmd[1] >= '0' && cmd[1] <= '9')
				/* nothing uses more than 1 digit */
				goto error;
			cmd_num = cmd[0] - '0';
			len--;
			cmd++;
			lcmd++;
		}

		switch (cmd_char) {
		case 'w':
			switch (cmd_num) {
			case 0:
				/* AT&W: save settings */
				/* force setting dirty */
				(void)EEPROM.getDataPtr();
				if (!EEPROM.commit())
					goto error;
				break;
			default:
				goto error;
			}
			break;
		case 'z': {
			/* AT&Z: manage bookmarks */
			uint32_t index = 0;
			uint8_t query;
			int chars = 0;

			if (sscanf(lcmd, "%u=%n", &index, &chars) == 1 &&
			    chars > 0) {
				/* AT&Zn=...: store address */
				query = 0;
			} else if (sscanf(lcmd, "%u?%n", &index, &chars) == 1 &&
			    chars > 0) {
				/* AT&Zn?: query stored address */
				query = 1;
			} else {
				errstr = strdup("invalid store command");
				goto error;
			}

			if (index < 1 || index > NUM_BOOKMARKS) {
				errstr = strdup("invalid index");
				goto error;
			}

			if (query) {
				outputf("\n%s\r\n",
				    settings->bookmarks[index - 1]);
			} else {
				memset(settings->bookmarks[index - 1], 0,
				    sizeof(settings->bookmarks[0]));
				strncpy(settings->bookmarks[index - 1],
				    cmd + 2,
				    sizeof(settings->bookmarks[0]) - 1);
			}
			break;
		}
		default:
			goto error;
		}

		/* consume all chars */
		len = 0;
		break;
	default:
		goto error;
	}

done_parsing:
	/* if any len left, parse as another command */
	if (len > 0)
		goto parse_cmd;

	if (olcmd)
		free(olcmd);

	if (!did_response && state == STATE_AT && !settings->quiet) {
		if (settings->verbal)
			outputf("%sOK\r\n", did_nl ? "" : "\n");
		else
			output("0\r");
	}

	return;

error:
	if (olcmd)
		free(olcmd);

	if (!did_response && !settings->quiet) {
		if (settings->verbal) {
			output("\nERROR");
			if (errstr != NULL)
				outputf(" %s", errstr);
			output("\r\n");
		} else
			output("4\r");
	}

	if (errstr != NULL)
		free(errstr);
}
