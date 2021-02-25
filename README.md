## WiFiStation: A WiFi interface to the MailStation

This is firmware for an ESP8266 that talks to a
[Cidco MailStation](https://jcs.org/2019/05/03/mailstation)
over its parallel port and allows it to make telnet connections over the
ESP8266's WiFi interface, as well as allow uploading code to the MailStation
from a remote computer over WiFi rather than a parallel LapLink (or
[USB](https://jcs.org/2020/03/31/mailstation_usb)
cable).

An
[MCP23017](http://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf)
is used to connect 12 GPIO lines (8 data, 2 input status, and 2 output control
lines) to pins on the MailStation's printer port, and communicate to the
ESP8266 via two I2C lines.

WiFiStation implements an
[AT command interface](https://en.wikipedia.org/wiki/Hayes_command_set)
that is simultaneously available over its TTL serial port for debugging and
development, in addition to interfacing through the MailStation.
