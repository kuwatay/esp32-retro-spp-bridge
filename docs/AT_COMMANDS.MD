# AT Command Reference

## Command Summary

| Command           | Description                            |
| ----------------- | -------------------------------------- |
| `AT`              | Test command                           |
| `ATI`             | Show version information               |
| `AT?`             | Show command summary                   |
| `ATD`             | Show running configuration             |
| `AT&V`            | Show running and startup configuration |
| `AT&W`            | Save configuration                     |
| `AT&Y`            | Load startup configuration             |
| `AT&F`            | Load factory defaults                  |
| `ATZ`             | Restart bridge                         |
| `ATO`             | Return to ONLINE mode                  |
| `ATO?`            | Show current mode                      |
| `ATH`             | Return NO CARRIER                      |
| `ATN?`            | Show Bluetooth name                    |
| `ATN=<name>`      | Set Bluetooth name                     |
| `ATP?`            | Show pairing PIN                       |
| `ATP=<pin>`       | Set pairing PIN                        |
| `ATB?`            | Show UART baud rate                    |
| `ATB=<baud>`      | Set UART baud rate                     |
| `ATCONSOLE?`      | Show console mode                      |
| `ATCONSOLE=AUTO`  | Automatic console selection            |
| `ATCONSOLE=BT`    | Bluetooth console                      |
| `ATCONSOLE=USB`   | USB console                            |
| `ATE?`            | Show local echo                        |
| `ATE=<0\|1>`      | Local echo control                     |
| `ATQ?`            | Show quiet mode                        |
| `ATQ=<0\|1>`      | Quiet mode control                     |
| `ATNOTIFY?`       | Show USB notification setting          |
| `ATNOTIFY=<0\|1>` | USB notification control               |
| `ATL?`            | Show link status                       |
| `ATC?`            | Show Bluetooth connection status       |
| `ATMAC?`          | Show Bluetooth MAC address             |
| `ATS12?`          | Show escape guard time                 |
| `ATS12=<ms>`      | Set escape guard time                  |

## Command Mode

The bridge starts in ONLINE mode.

Enter COMMAND mode:

```text
idle for S12 ms
+++
idle for S12 ms
```

Return to ONLINE mode:

```text
ATO
```

## ATS12

Escape guard time in milliseconds.

| Value      | Description            |
| ---------- | ---------------------- |
| `0`        | Immediate escape mode  |
| `250-5000` | Hayes-style guard time |

Example:

```text
ATS12=1000
```

or

```text
ATS12=0
```

for buffered terminal software.

## Configuration Example

Set a new Bluetooth device name:

```text
ATN=RETRO_BRIDGE
AT&W
ATZ
```

Set a new pairing PIN:

```text
ATP=4321
AT&W
ATZ
```

Enable automatic console selection:

```text
ATCONSOLE=AUTO
AT&W
```

Enable local echo:

```text
ATE=1
AT&W
```

## Notifications

When notifications are enabled:

```text
ATNOTIFY=1
```

The USB maintenance console displays:

```text
[BT CONNECTED]
[CONSOLE BT]

[BT DISCONNECTED]
[CONSOLE USB]

[CONSOLE AUTO]
[CONSOLE BT]

[CONSOLE USB]
```

## Notes

* Configuration changes become permanent after `AT&W`.
* Bluetooth name and PIN changes require `ATZ`.
* During COMMAND mode, retro-computer data is discarded and not buffered.
* `ATH` does not disconnect Bluetooth. It only returns `NO CARRIER`.
