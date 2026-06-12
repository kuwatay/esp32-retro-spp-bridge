# ESP32 Retro SPP Bridge

A Bluetooth Classic SPP bridge for retro computers, terminals, and embedded systems.

This project turns an ESP32 into a transparent serial bridge with a Hayes-style command interface inspired by classic serial modems.

![System Overview](docs/images/system-overview.png)

## Features

* Bluetooth Classic SPP support
* Transparent serial bridge
* Hayes-style AT command interface
* ONLINE / COMMAND mode operation
* Configurable Bluetooth device name
* Configurable pairing PIN
* Configurable UART baud rate
* Persistent configuration storage (NVS)
* Guard-time escape sequence (`+++`)
* Automatic console selection
* USB maintenance console
* Status LEDs

## Hardware

Tested with:

* ESP32-WROOM-32
* Wemos D1 R32

Default UART connection:

| Signal | GPIO   |
| ------ | ------ |
| RX     | GPIO16 |
| TX     | GPIO17 |

`Serial2` is always connected to the retro computer.

## Console Modes

The bridge supports three console modes.

### AUTO (Default)

```text
BT disconnected -> USB console
BT connected    -> Bluetooth console
```

### BT

```text
Bluetooth SPP <-> ESP32 <-> Serial2
```

### USB

```text
USB Serial <-> ESP32 <-> Serial2
```

## Status LEDs

Default GPIO assignments:

| Function | GPIO   | Description       |
| -------- | ------ | ----------------- |
| BT       | GPIO2  | Bluetooth status  |
| RX       | GPIO25 | Receive activity  |
| TX       | GPIO26 | Transmit activity |
| CMD      | GPIO27 | COMMAND mode      |

### BT LED

```text
Blinking : Waiting for connection
ON       : Bluetooth connected
```

### RX LED

```text
Flash    : Retro Computer -> Console
```

### TX LED

```text
Flash    : Console -> Retro Computer
```

### CMD LED

```text
OFF      : ONLINE mode
ON       : COMMAND mode
```

## Default Settings

```text
NAME=ESP32_SPP_TEST
PIN=1234
BAUD=115200

CONSOLE=AUTO

QUIET=1
ECHO=0
NOTIFY=1

S12=1000
```

## Entering Command Mode

Default Hayes-compatible escape sequence:

```text
idle for S12 ms
+++
idle for S12 ms
```

For buffered terminal software:

```text
ATS12=0
```

enables immediate escape detection.

## Documentation

AT command reference:

* `docs/AT_COMMANDS.md`

## About This Project

This project was created with extensive assistance from OpenAI ChatGPT.

ChatGPT was used as a collaborative development tool during the design, implementation, testing, and documentation phases of the project.

Final design decisions and validation were performed by the project author.

## License

MIT License
