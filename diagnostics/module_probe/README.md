# TempLogPro module probe

Separate diagnostic firmware, not a logger or battery-life benchmark.
The working application sources and Release image are unchanged.

## Current state

Installed and verified on the device on 2026-09-19. USB CDC is COM3 on this PC.
The LCD displays `dIA` using the existing segment driver when the existing RTC
clock is available. It does not provide the normal menu or record measurements.
The composite USB storage interface deliberately reports **no medium**.
Do not format it. The measurement flash remains accessible only by bounded
read-only diagnostic commands.

## Safety boundary

- No logger, configuration, RTC-initialization or report/filesystem objects linked.
- No write-enable, program or erase routine for external flash in the final image.
- No EEPROM writes, option-byte changes or backup-domain reset.
- Sensor requests only write its register pointer (0..3), then read data.
  They do not write sensor configuration/thresholds or send a general-call reset.
- GPIO reads are passive. Initialization reuses the known LCD, LED, SPI and I2C
  pin mappings, not a speculative search by toggling unknown pins.
- VDD is measured through internal VREFINT. It is **not** a battery terminal measurement.
- The already selected LSI clock is restarted after NRST, if necessary. Calendar,
  prescalers and clock selection are not written. Time can pause while LSI is off
  during programming; check/synchronize time when returning to normal logging.
- Regular USB clocks, SysTick, LCD and shallow WFI remain active. Do not use this
  image's current consumption as a measurement of the main firmware's STOP mode.

## Build and inspect

From the project directory:

```powershell
./tools/build_module_probe.ps1
python tests/module_probe_vectors.py
./tools/read_module_probe.ps1 -Port COM3
./tools/read_module_probe.ps1 -Port COM3 -BackupFlash
```

The build is independent of Release objects. Its map/symbol audit rejects linked
application, logger, configuration, flash-write and original console handlers.
The ARM binary test covers command isolation, line framing, malformed/long input,
flash bounds, JEDEC result semantics and unknown-chip rejection (64 checks).
The flash backup reads 512 KiB twice and requires matching SHA256 hashes.

## True STOP baseline

Build the isolated STOP image with:

```powershell
./tools/build_module_probe.ps1 -Stop
```

This image initializes only HAL and the low-power clock profile, disables
SysTick and enters `HAL_PWR_EnterSTOPMode`. It does not start USB, LCD, I2C,
SPI/Winbond, ADC, LEDs or the logger. The RTC/LSI state is intentionally left
unchanged so the test does not alter the device clock domain. Flash it only for
the current measurement; wake/reset it with ST-Link, then restore the main
image with `restore_main_after_probe.ps1`.

Measure with USB physically disconnected and ST-Link disconnected from the
device. If the meter range is in microamps, wait a few seconds after reset and
record the stable value. This is the board's STOP baseline, not the current of
the MCU package alone, because connected passive components and RTC/LSI may
still contribute.

After flashing, Windows may retain a stale serial handle briefly. The reader
fails closed on a wrong firmware identity or transport error. Close the handle
and rerun after re-enumeration; do not send normal logger/config commands.

## Protocol

115200 baud, 8N1, DTR enabled, ASCII lines. Send one request, wait for `END`.
Replies are delimited by `BEGIN` and `END`. Numbers in register dumps are hex;
sensor tick_ms and len are decimal. A one-byte sensor read prints a padded zero
as its second displayed byte; only the first byte is valid for len=1.

```text
PING
HELP
GET MCU
GET CLOCKS
GET GPIO A
GET GPIO B
GET GPIO C
GET GPIO D
GET GPIO E
GET RTC
GET SENSOR
GET FLASH
GET VDD
FLASH READ 000000
```

`FLASH READ` takes exactly six hex digits, 256-byte aligned, 000000..07FF00.
It always returns one 256-byte block and requires JEDEC EF3013.

## Differential STOP test

Build the peripheral-shutdown image with:

```powershell
./tools/build_module_probe.ps1 -StopDevices
```

This version explicitly powers down Winbond, puts the verified I2C temperature
sensor into shutdown, disables the unused LCD/ADC/USB blocks, and then enters
the same true STOP loop as the baseline image. It does not initialize the
logger or alter EEPROM, RTC calendar data, thresholds, or external flash
contents.

The sensor shutdown survives MCU reset. After measuring, flash the regular
`-NoLcd` probe, send `SENSOR WAKE`, verify the baseline configuration, and only
then restore the main firmware.

## Differential STOP test with rescue path

For a measurement where NRST recovery is inconvenient, build the protected
variant:

```powershell
./tools/build_module_probe.ps1 -StopDevicesRescue
```

It performs the same Winbond/sensor shutdown and true STOP entry as
`-StopDevices`. Before entering STOP it checks the two active-low buttons on
PA0 and PA1 for one second. If both are held during power-up, the image stays
in shallow WFI instead, so ST-Link can attach normally and replace the image.
This is the recovery path after the current measurement: remove power, hold
both buttons, reconnect power, then flash the regular `-NoLcd` probe.

## Return to main firmware

Backup: `backup/main_before_module_diag_20260919_231527/`.
It contains the source snapshot, working ELF/HEX/BIN/MAP, actual 256 KiB MCU
flash image, 8 KiB data EEPROM and copied USB data files.

```powershell
./tools/restore_main_after_probe.ps1
```

This explicitly flashes the checksum-pinned main BIN, verifies it, then resets.
It does not restore or erase the EEPROM or external measurement memory. Stored
logger state may resume recording. Synchronize the clock before a new timed test.
The full raw MCU/EEPROM backups are recovery copies, not routine restore inputs.

Readings and two matching Winbond dumps:
`results_20260919_233136/`. Interpretation:
`../../docs/module_probe_findings_2026-09-19.md`.
