# USB modes and sensor shutdown: main 0.8.13-usb-modes

## User behavior

Settings contains a PL/EN USB operating mode selector. Send settings to persist
the choice; changing the dropdown alone does not change the device.

- `usb_logging=1`: measurements and logging are allowed while connected to USB,
  including operation powered solely from USB. Existing START/STOP controls and
  the configured interval still apply. Selecting this mode does not itself START
  a stopped logger.
- `usb_logging=0`: a detected USB connection is for settings and reports. A
  running logger records its usual STOP event. An in-flight conversion is
  cancelled and cannot append a sample after STOP. START is refused with E007
  while in this mode. Disconnecting USB leaves the logger stopped; explicitly
  START with the buttons, or enable USB logging and START from the panel.
- Battery operation retains its schedule, RTC, LCD status and button wakeups.

Existing configurations migrate in RAM with USB logging enabled, preserving
previous behavior. Configuration v3 uses the existing reserved word at offset
40, retains the 44-byte record and existing config sector, and accepts v1/v2.
Saving a v3 configuration makes it unreadable by the old v2-only firmware;
keep the original config/flash backup when rolling back firmware.

The existing cable detector infers a USB host from D+/D- and bus activity. It is
not a direct VBUS or battery-current measurement. A power-only supply without a
detected host may run the battery clock profile even though power is from USB.
This change does not alter detection, USB clocks or physical power routing.

## Sensor and other consumers

The main sensor driver now uses the exact board-tested configuration family
60A0/E1A0. It writes SD (61A0) during initialization, including after diagnostic
firmware left the sensor in shutdown. Each scheduled sample requests one-shot
(E1A0), polls readiness at most once per 10 ms, reads only a completed sample,
and leaves SD set. The timeout is 200 ms. Unknown configurations are rejected
without speculative writes. Failure cleanup requests shutdown once; no retry
loop holds up USB or buttons. The next scheduled sample can retry initialization.

The observed one-shot experiment completed repeatedly in about 27 ms:
`diagnostics/module_probe/results_20260919_234953/registers.json`.
The compatible protocol is documented by TI:
https://www.ti.com/lit/ds/symlink/tmp102.pdf (shutdown and one-shot sections).
This does not uniquely identify the actual chip as TMP102 versus TMP112 or a
compatible device.

PB6/PB7 transfers use the bounded, clock-stretch-aware sequence verified in the
probe. Delay scales with SystemCoreClock so battery operation does not stretch
every transaction by the 32 MHz / 2.1 MHz ratio. Physical timings on the new main
image still need hardware verification.

Winbond already receives B9 after each synchronous read/write/erase and AB
before the next operation; SPI1 is clock-gated in between. USB report reads
therefore wake it briefly. The driver has not been replaced. ADC already restores
its power/clock state after a read. LCD remains active for the requested icons.
The application prevents the one-second MCU STOP while a conversion is pending,
using the existing shallow idle path until completion. USB retains its active
clock and idle behavior for MSC/CDC communication.

The reported approximately 60 uA was a simplified diagnostic STOP test with LCD
disabled. The difference from about 90 uA cannot be attributed to one peripheral
because several states changed together. It does not establish main-firmware
idle current or resolve the earlier battery current reported with active USB.
Neither zero USB battery drain nor a specific battery lifetime is claimed.

## Verification and recovery

- Release build completed without new compiler warnings.
- `python tests/usb_modes_vectors.py`: 201 checks on actual Release ARM code,
  with mocked I/O, covering config versions, persistence, invalid options,
  one-shot readiness/errors/cancellation and USB mode transitions.
- `python tests/usb_idle_vectors.py`: 63 checks for each of four build variants,
  including no deep STOP during conversion.
- `node tests/usb_modes_panel.cjs`: Edge desktop 1440 px and mobile 390 px,
  PL/EN, selector bounds, CDC/config round-trip, fragmented acknowledgements and
  live logger status refresh. Screenshots in `tmp/usb_modes_*.png`.

Source/build backup before this change:
`backup/main_before_usb_modes_20260920_043904/`.
The original pre-diagnostic main image and hardware memory backups remain in
`backup/main_before_module_diag_20260919_231527/`.

Prepared outputs: `Release/TempLogPro.bin`, `.hex`, `.elf`.

## Hardware deployment

### Battery error follow-up, 2026-09-21

After a subsequent battery run, ERROR recurred at 189 records. Hot-plug SWD
reported FLASH_WRITE, with sensor/RTC OK. With USB disconnected, direct sensor
reads succeeded: temperature 0x1A60 (26.375 C), config 0xE1A0, core 2097152 Hz.
MCU VDD read 3321 mV; this alone does not establish physical battery isolation.
The user authorized formatting. A full 524288-byte backup was saved to
`backup/before_battery_flash_diag_20260921/winbond.bin`, then the existing logger
format routine returned success. No history restoration was attempted.

A main-image diagnostic was added: `g_hw_flash_failure[4]` retains the first
failed write/erase as operation (1 write, 2 erase), address, driver result and
HAL tick. Driver results: 1 range, 2 busy/SPI timeout, 3 write-enable, 4 verify.
It changes no successful I/O behavior and resets on MCU reset. The image built,
passed 201 ARM checks, was flashed and verified. Battery logging needs a physical
START; an attempted SWD session after sleep did not connect, so its proposed
temporary 1-second configuration was NOT applied. Interval remains 5 s.
The underlying initial flash failure is not fixed or proven to be a sensor fault.

The earlier CDC CLEAR failure is explained by `templogpro_app_clear_logger()`
rejecting app ERROR before reaching the format routine. It was not evidence of
an erase-command failure.

The main image was flashed successfully after entering the diagnostic rescue
path. STM32CubeProgrammer identified device 0x427 (256 KiB), programmed 131472
bytes at 0x08000000, reported `Download verified successfully`, and issued a
software reset. No mass erase, option-byte changes or external flash erase was
requested. Programmed BIN SHA-256:
`EFA2D69553CFC0441A2A85DE44C215217B26602AB4C97013C4B75FA99BE43434`.

Windows then enumerated the TEMPLOGPRO mass-storage device and CDC port COM3.
After reconnection on 2026-09-21, CDC PING, GET CONFIG, LOGGER STATUS and GET
POWER all responded. The existing config reports `usb_logging=1`, interval 5 s,
limits 24/31.5 C, battery mode enabled and LEDs disabled. The MCU reports USB,
32 MHz and VDD approximately 3.33 V. RTC synchronization returned OK TIME.
Initially the logger was in app ERROR, with 16608 records. Recovery and the
subsequent successful mode-switch tests are recorded below.

ST-Link hot-plug RAM inspection reports RTC/sensor/flash-read/USB OK, with last
error `TEMPLOGPRO_ERROR_FLASH_WRITE`. Read-only copies of both metadata sectors
and the complete external flash show a storage inconsistency:

- Metadata sector A (0x1000) and first record sector (0x3000) are erased.
- Sector B's latest valid metadata advertises 16608 records; later records
  already occupy the following addresses. A write verification failure at that
  occupied slot prevents further appends.
- The earlier double-verified backup contains 16637 valid records and newer
  valid metadata in sector A. An incomplete record 16638 is already present in
  that older backup. The exact cause/timing of the erased sectors is not known.
- Current raw contents also have a damaged record 16609, consistent with the
  attempted append at the stale next-write address.

Pre-recovery USB files and the full current raw flash are saved in
`backup/usb_modes_hardware_20260921_213142/`. Current raw SHA-256:
`D07F148B184C215BA9DAD9C3C86DE5F3FB9F20911601FD6C9C00A979EDBD543D`.
Earlier raw backup is in `diagnostics/module_probe/results_20260919_233136/`,
SHA-256 `0DC95557D52DD99DE95122EC768E89E71CCF9A5A2F11E3C0571331A655DE4BDE`.
The user explicitly permitted formatting instead of restoring the history.
The first CDC `LOGGER CLEAR CONFIRM` failed. Direct invocation of the existing
`templogpro_logger_format()` through ST-Link/GDB succeeded, followed by an MCU
reset. This was a logical logger format, not a full external-flash erase. Old
history is retained in the host backups; it was not restored to the logger.
The initial CDC format failure has not been conclusively explained and should
not be counted as a passed USB-clear test.

Hardware verification after formatting:

- USB logging produced real samples every 5 s, approximately 25.25-25.31 C.
- Saving `usb_logging=0` returned OK CONFIG and switched RUN to PAUSE/STOPPED.
  START returned E007 as intended. Record count remained 12 over a 7 s wait.
- Saving `usb_logging=1` preserved STOP until explicit START. After START, record
  count rose to 16 and sample count to 13 over 11 s; app remained RUN.
- After another MCU reset, USB re-enumerated, persisted config generation 15
  retained `usb_logging=1`, and the logger continued recording: 22 records,
  19 samples, RUN/LOGGING, interval 5 s. Limits 24/31.5 C, battery mode enabled
  and LEDs disabled were retained.

No additional firmware changes were needed during this hardware session.
The 201 ARM-image checks and four sets of 63 low-power checks passed again on
2026-09-21. Main-firmware battery current remains unmeasured. USB-powered logging
was verified; physical battery isolation cannot be inferred from software.

For recovery from the old StopDevicesRescue diagnostic, boot holding PA0 and PA1
for one second to stay in shallow WFI and regain ST-Link access. Flash only main
program memory; do not mass erase, change option bytes or erase external
measurement storage. The new main handles sensor shutdown itself; no
continuous-mode wake command is required.
