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

For the latest documentation for this project, please see
[my WiFiStation site](https://jcs.org/wifistation).

## Hardware Details

An
~~[MCP23017](http://ww1.microchip.com/downloads/en/DeviceDoc/20001952C.pdf)~~
[MCP23S18](http://ww1.microchip.com/downloads/en/devicedoc/22103a.pdf)
connects to an
[Adafruit Feather HUZZAH ESP8266](https://www.adafruit.com/product/2821)
over ~~I2C~~ SPI and has 12 of its GPIO lines (8 data, 2 input status, and 2
output control) routed to pins on a DB25 connector, which plugs into the
MailStation's printer port.
