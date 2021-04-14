# WiFiStation: A WiFi interface to the MailStation

This project is a set of firmware, tools, and hardware specifications for a
board housing an ESP8266 module which talks to a
[Cidco MailStation](https://jcs.org/2019/05/03/mailstation)
over its parallel port.

WiFiStation acts as a modem with an
[`AT` command interface](https://en.wikipedia.org/wiki/Hayes_command_set)
allowing the MailStation to issue `AT` commands to "dial" to telnet addresses
rather than phone numbers.
This allows the MailStation to use software like
[msTERM](https://github.com/jcs/msTERM)
to connect to BBSes over WiFi rather than its internal modem.

All communication between the WiFiStation and the MailStation is also echoed on
the WiFiStation's USB serial interface, so commands can be sent from a host
computer as well.

## Configuring WiFi

The Adafruit Feather HUZZAH ESP8266 module used includes a SiLabs CP2104 USB
Serial interface, so just a micro-USB cable is needed to connect to the
WiFiStation from a host machine.

The default baud rate is 115200 (`8N1`) and can be changed after connecting
with `AT$SB=...` to a supported rate of 110, 300, 1200, 2400, 4800, 9600,
19200, 38400, 57600, or 115200.

Connecting with a terminal program like `cu` and typing `AT` should return
`OK`:

	$ cu -l /dev/cuaU0 -s 115200
	Connected to /dev/cuaU0 (speed 115200)
	AT
	OK

The WiFi SSID and WPA key can be configured with `AT$SSID=...` and
`AT$PASS=...`.
Once configured, `ATI` can be issued to display the status of the WiFi
connection:

	$ cu -l /dev/cuaU0 -s 115200
	Connected to /dev/cuaU0 (speed 115200)
	ATI
	Firmware version:  0.1
	Serial baud rate:  115200
	Default WiFi SSID: example
	Current WiFi SSID: example
	WiFi Connected:    yes
	IP Address:        192.168.1.100
	Gateway IP:        192.168.1.1
	DNS Server IP:     8.8.8.8
	HTTP Server:       yes
	OK

At this point you should be able to `ping` the WiFiStation on your network at
the IP address shown.

### HTTP Web Server

To enable the HTTP server, issue `AT$HTTP=1`.
You should now be able to reach the WiFiStation's web interface at the standard
port 80.

## Saving Changes

To make any changes issued persist after powering off the WiFiStation, issue
`AT&W`.

## Uploading Code

The WSLoader program in the `mailstation/` directory is a small Z80 assembly
program to be installed onto the MailStation.
Once it is running, it waits for data to be delivered over the MailStation's
parallel port, copies it into memory, and then executes it.

WiFiStation includes an `AT$UPLOAD` command that can be issued via its serial
interface (to a computer) to send binary files to the MailStation.
The `sendload` utility in the `host/` directory uses this command to upload a
binary from the host computer over its serial interface to the WiFiStation,
which then transfers it to the MailStation over its parallel port, where it is
read and executed by WSLoader.

Files can also be uploaded over the WiFiStation's HTTP interface, if enabled.
This can be done in a browser, or through a command-line utility like `curl`,
as long as it properly follows HTTP 307 redirects:

	curl -L -F file=@msterm.bin http://<WiFiStation IP>/upload

## Hardware Details

An
~~[MCP23017](http://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf)~~
[MCP23S18](http://ww1.microchip.com/downloads/en/devicedoc/22103a.pdf)
connects to an
[Adafruit Feather HUZZAH ESP8266](https://www.adafruit.com/product/2821)
over ~~I2C~~ SPI and has 12 of its GPIO lines (8 data, 2 input status, and 2
output control) routed to pins on a DB25 connector, which plugs into the
MailStation's printer port.
