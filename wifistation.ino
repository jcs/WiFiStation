#include "wifistation.h"

enum {
	STATE_IDLE,
	STATE_UPLOAD,
};

static int state = STATE_IDLE;
static char curcmd[128] = { 0 };
static unsigned int curcmdpos = 0;

void
loop(void)
{
	int c;
	char b;

#if 0
	if ((c = msread()) != -1)
		Serial.write(c);
#endif

	if (!Serial.available())
		return;

	b = Serial.read();

	switch (state) {
	case STATE_IDLE:
		if (b == '\r' && Serial.peek() == '\n')
			Serial.read();

		/* USR modem mode, ignore input not starting with 'at' */
		if (curcmdpos == 0 && (b != 'A' && b != 'a')) {
			break;
		} else if (curcmdpos == 1 && (b != 'T' && b != 't')) {
			outputf("\b \b");
			curcmdpos = 0;
			break;
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
		break;
	default:
		output("unknown state ");
	}
}

void
exec_cmd(char *cmd, size_t len)
{
	if (len < 2 ||
	    (cmd[0] != 'A' && cmd[0] != 'a') ||
	    (cmd[1] != 'T' && cmd[1] != 't'))
		goto error;

	if (len == 2) {
		output("OK\r\n");
		return;
	}

	switch (cmd[2]) {
	case 'I':
	case 'i':
		if (len > 4)
			goto error;
		switch (len == 3 ? '0' : cmd[3]) {
		case '0':
			/* ATI or ATI0 */
			outputf("WiFi SSID:      %s\r\n", WiFi.SSID());
			outputf("WiFi Connected: %s\r\n",
			    WiFi.status() == WL_CONNECTED ? "yes" : "no");
			if (WiFi.status() == WL_CONNECTED) {
				outputf("IP Address:     %s\r\n",
				    WiFi.localIP().toString().c_str());
				outputf("Gateway IP:     %s\r\n",
				    WiFi.gatewayIP().toString().c_str());
				outputf("DNS Server IP:  %s\r\n",
				    WiFi.dnsIP().toString().c_str());
			}
			output("OK\r\n");
			break;
		case '1': {
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
	default:
		goto error;
	}

	return;

error:
	output("ERROR\r\n");
}
