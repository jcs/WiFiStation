#include "wifistation.h"

static char curcmd[128] = { 0 };
static unsigned int curcmdpos = 0;

void
loop(void)
{
	int b = -1;

	if ((b = ms_read()) != -1)
		mailstation_alive = true;
	else if (Serial.available() && (b = Serial.read()))
		serial_alive = true;
	else
		return;

	/* USR modem mode, ignore input not starting with 'at' */
	if (curcmdpos == 0 && (b != 'A' && b != 'a')) {
		return;
	} else if (curcmdpos == 1 && (b != 'T' && b != 't')) {
		outputf("\b \b");
		curcmdpos = 0;
		return;
	}

	switch (b) {
	case '\r':
	case '\n':
		output("\r\n");
		curcmd[curcmdpos] = '\0';
		exec_cmd((char *)&curcmd, curcmdpos);
		curcmd[0] = '\0';
		curcmdpos = 0;
		break;
	case '\b':
	case 127:
		if (curcmdpos) {
			output("\b \b");
			curcmdpos--;
		}
		break;
	default:
		curcmd[curcmdpos++] = b;
		output(b);
	}
}

void
exec_cmd(char *cmd, size_t len)
{
	unsigned long t;

	char *lcmd = (char *)malloc(len);
	if (lcmd == NULL)
		return;

	for (size_t i = 0; i < len; i++)
		lcmd[i] = tolower(cmd[i]);
	lcmd[len] = '\0';

	if (len < 2 || lcmd[0] != 'a' || lcmd[1] != 't')
		goto error;

	if (len == 2) {
		output("OK\r\n");
		return;
	}

	switch (lcmd[2]) {
	case 'i':
		if (len > 4)
			goto error;

		switch (len == 3 ? '0' : cmd[3]) {
		case '0':
			/* ATI or ATI0: show settings */
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
		default:
			goto error;
		}
		break;
	case 'z':
		output("OK\r\n");
		ESP.reset();
		break;
	case '$':
		/* wifi232 commands */

		if (strcmp(lcmd, "at$ssid?") == 0) {
			/* AT$SSID?: print wifi ssid */
			outputf("%s\r\nOK\r\n", settings->wifi_ssid);
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
		} else if (strcmp(lcmd, "at$pass?") == 0) {
			/* AT$PASS?: print wep/wpa passphrase */
			outputf("%s\r\nOK\r\n", settings->wifi_pass);
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
		} else if (strncmp(lcmd, "at$upload", 9) == 0) {
			/* AT$UPLOAD: mailstation program loader */
			int bytes = 0;
			unsigned char b;

			if (sscanf(lcmd, "at$upload%u", &bytes) != 1 ||
			    bytes < 1)
				goto error;

			/*
			 * Only use Serial writing from here on out, output()
			 * and outputf() will try to write to the MailStation.
			 */

			/* send low and high bytes of size */
			if (ms_write(bytes & 0xff) != 0 ||
			    ms_write((bytes >> 8) & 0xff) != 0) {
				Serial.printf("ERROR MailStation failed to "
				    "receive size\r\n");
				break;
			}

			Serial.printf("OK send your %d byte%s\r\n", bytes,
			    bytes == 1 ? "" : "s");

			t = millis();
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
				Serial.write(b);
				bytes--;
				t = millis();
			}

			if (bytes == 0)
				Serial.print("\r\nOK\r\n");
			else
				Serial.printf("\r\nERROR MailStation failed to "
				    "receive byte with %d byte%s left\r\n",
				    bytes, (bytes == 1 ? "" : "s"));

			break;
		} else if (strcmp(lcmd, "at$watch") == 0) {
			/* AT$WATCH: watch MCP23017 lines for debugging */
			uint16_t prev = UINT16_MAX;
			int i;

			ms_datadir(INPUT);

			for (;;) {
				yield();

				/* watch for ^C */
				if (Serial.available() && Serial.read() == 3)
					break;

				uint16_t all = ms_status();
				if (all != prev) {
					Serial.print("DATA: ");
					for (i = 0; i < 8; i++)
						Serial.print((all & (1 << i)) ?
						    '1' : '0');

					Serial.print(" STATUS: ");
					for (; i < 16; i++)
						Serial.print((all & (1 << i)) ?
						    '1' : '0');
					Serial.print("\r\n");
					prev = all;
				}
			}
			Serial.print("OK\r\n");
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
	output("ERROR\r\n");
}
