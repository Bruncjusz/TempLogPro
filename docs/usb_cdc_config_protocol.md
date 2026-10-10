# TempLogPro USB CDC configuration protocol

Date: 2026-08-05

This document describes the composite USB transport used by the current
firmware. The device exposes Mass Storage for report files and CDC ACM for
Web Serial configuration from `TEMPLOGPRO.HTML`.

## Goals

- Keep Mass Storage read access stable.
- Add CDC ACM so Chrome and Edge can use Web Serial from `TEMPLOGPRO.HTML`.
- Avoid writes from the browser directly touching Winbond sectors.
- Route every command through application-level validation.
- Keep the protocol line-oriented and easy to debug with a serial terminal.

## Framing

All commands are ASCII lines ending in `\n`.

Responses are ASCII lines ending in `\n`.

Maximum command line length: 96 bytes.

Numbers are decimal unless a field explicitly starts with `0x`.

## Commands

```text
PING
GET STATUS
GET POWER
GET CONFIG
SET TIME YYYY-MM-DD HH:MM:SS
SET INTERVAL seconds
SET TEMP LOW millidegC
SET TEMP HIGH millidegC
SET BATTERY ON
SET BATTERY OFF
SET LCD MODE number
SET PDF ON
SET PDF OFF
SET LED ON
SET LED OFF
CONFIG BEGIN
key=value
...
CONFIG END
LOGGER START
LOGGER PAUSE
LOGGER CLEAR
LOGGER CLEAR CONFIRM
LOGGER STATUS
```

## Responses

```text
OK command
ERR code text
STATUS key=value ...
CONFIG key=value ...
```

Initial error codes:

| Code | Meaning |
|---|---|
| `E001` | Unknown command |
| `E002` | Bad argument |
| `E003` | Command not allowed in current logger state |
| `E004` | Flash/config write failed |
| `E005` | RTC update failed |
| `E006` | Destructive command requires explicit confirmation |

## Validation rules

- `SET TIME` accepts years 2024..2099.
- `SET INTERVAL` accepts 1..86400 seconds.
- Temperature limits are stored in millidegrees C in the protocol and converted
  to the firmware's internal x256 format.
- `SET LCD MODE` accepts the existing app view numbers 0..11.
- `CONFIG BEGIN` / `CONFIG END` carries the same keys as `CONFIG.TXT`:
  `interval_seconds` (1..600), `temperature_low_c`, `temperature_high_c`,
  `battery_mode`, `usb_logging`, `lcd_mode`, `lcd_menu_mask`, `pdf_enable`,
  `led_enable`, `temperature_unit` (`C` or `F`, LCD only; stored data stays
  in Celsius).
- If both individual `SET ...` commands and a config block are present in one
  payload, the parser should validate the whole payload first and commit config
  changes once.
- `LOGGER STATUS` is read-only and reports app state, logger state, record
  count, capacity, interval, active seconds and temperature statistics.
- `GET POWER` is read-only and reports the detected source, VDD derived from
  VREFINT, clock, software-estimated CPU duty, WFI/STOP time and wakeup counts.
- `LOGGER START` and `LOGGER PAUSE` route through the same application API as
  the physical long-press actions.
- `LOGGER CLEAR` is intentionally rejected with `E006`; the browser must send
  `LOGGER CLEAR CONFIRM` after a user confirmation dialog.
- Commands that change flash-backed config must report success only after the
  write verifies.

## Current implementation status

- Composite USB enumerates on Windows as Mass Storage plus `USB Serial Device`.
- CDC endpoint layout:
  - interface 0: CDC control
  - interface 1: CDC data
  - interface 2: MSC
  - EP1 bulk IN/OUT: MSC
  - EP2 bulk IN/OUT: CDC data
  - EP3 interrupt IN: CDC notification
- Implemented commands: `PING`, `GET STATUS`, `GET POWER`, `GET CONFIG`, `SET TIME`,
  `CONFIG BEGIN` / `CONFIG END`, `LOGGER STATUS`, `LOGGER START`,
  `LOGGER PAUSE`, `LOGGER STOP`, `LOGGER CLEAR`, `LOGGER CLEAR CONFIRM`.
- `CONFIG END` writes through the existing verified config storage path.
- Logger control commands route through `templogpro_app_*()` so LCD/app state and
  logger state stay consistent.

## Verified on device

```text
Windows volume = TEMPLOGPRO
Windows CDC port = USB Serial Device (COM3)
PING response = OK PING
GET STATUS response = STATUS usb=cdc msc=1
CONFIG write response = OK CONFIG
```
