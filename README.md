# AVR based wiegand door controller

This project is about building an open source physical access control
system for small communities and groups.

## Electronic

In this directory you will find the hardware design (schematics and PCB) for
a two door controller using an Arduino Nano. Its main features are:

* Can be powered with 12V or 24V.
* Support various setup for the door lock, the relay can be connected to
  12V, 24V or GND as normally open or as normally closed.
* An USB power supply to power additional devices such as an AP for network
  connectivity.
* Use RJ-45 connectors for the doors to allow standardizing the physical
  installation.

This design intentionally doesn't provide network connectivity for two reasons.
First we want to keep the device as cheap and reliable as possible. But most
importantly we want a secure system, implementing secure communication (SSL)
on a small micro-controller is not really possible. So instead of using dubious
Ethernet expansion boards we decided to only provide an USB interface and rely
on external devices running a full OS for access over the network.

### Pin out

	Pin | Function | T-568 A      | T-568 B      | Reader
	------------------------------------------------------
	  1 | Beep     | White Green  | White Orange | Yellow
	  2 | LED      | Green        | Orange       | Blue
	  3 | Status   | White Orange | White Green  |
	  4 | D1       | Blue         | Blue         | White
	  5 | D0       | White Blue   | White Blue   | Green
	  6 | Relay    | Orange       | Green        |
	  7 | 12V      | White Brown  | White Brown  | Red
	  8 | GND      | Brown        | Brown        | Black

## Firmware

In this directory you will find the firmware for the above hardware,
it supports both the Arduino Nano version 2 and version 3. We recommend using
version 3 as they have a much large EEPROM which allow for up to 200 access
records.

The firmware currently support:

* 26 bits wiegand RFID readers
* RFID reader with keypad for PIN
* Access with PIN only, card only or card + PIN
* Simple access records management over USB

The following is planned:

* Support external EEPROM for more access records
* Support RTC to allow various time dependent access control
* 34 bits RFID cards

## Daemon

In this directory you will find a daemon that allow managing one or more
controllers using OpenWrt ubus. As OpenWrt include a JSON-RPC interface to ubus
this daemon allow to manage the controllers over the network using a JSON-RPC
interface.

## Client

In this directory you will find the client software to manage the controllers.
First is a tool (and python API) that allow to manage a controller, either over
USB or over JSON-RPC. This tool is pretty low level but make it easy to setup
a single controller with a couple of users. Then there is database backed tool
that allow to easily manage larger deployments with several controllers and many
users.
