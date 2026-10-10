# TempLogPro - normal firmware idle update, 2026-09-19

## Scope

Completed the small full-firmware changes prepared before the overnight pause.
This is NOT the USB power-path diagnostic build. `FIRMWARE_MODE=5` and
`TEMPLOGPRO_USB_POWER_PATH_DIAG=0`. Version string remains `0.8.12-power`.

- USB may use shallow CPU WFI even when battery_mode is OFF. USB peripheral,
  clocks, SysTick, SOF and mass-storage remain enabled and unchanged.
- Flash busy still prevents idle. Battery-only idle eligibility is unchanged.
- Deep STOP is still forbidden while USB is present.
- CDC RX or queued, not-yet-started TX prevents idle. In-flight TX can complete
  in its interrupt while the CPU sleeps.
- Queue check and WFI are in a short PRIMASK critical section. A pending enabled
  interrupt wakes WFI; its handler runs after PRIMASK is restored. Calls from
  an ISR or with interrupts already masked do not enter idle.
- CDC TX indices and busy flag shared with interrupts are volatile.
- Added `Inc/templogpro_usb.h` instead of duplicating USB declarations.

No changes to logger structure, timestamps, sampling interval, files, RTC,
LCD, buttons or USB clocks in this patch. The earlier battery improvements
described in `battery_idle_audit_2026-08-25.md` remain in this build.

IMPORTANT: WFI already existed when battery_mode was ON. The device currently
has battery_mode=1, so this change alone is not evidence of a large current
reduction. Hardware power-path and battery-current questions remain open.

## Tests

`python tests/usb_idle_vectors.py` compiles six production function definitions
verbatim with ARM GCC and executes them in Unicorn (PC dependency only).
Firmware headers supply real types. IRQs, SCB, tick and external dependencies
are mocked. This is a logic test, not a model of physical USB or MCU current.

62 checks passed in each combination of O0/Os and WFI disabled/enabled:

- USB/battery/error/Flash-busy eligibility matrix;
- STOP guards for LCD, buttons, action, statistics and LEDs;
- RX/TX queues, wrapping indices, USB unconfigured and in-flight TX;
- interrupt arrival on both sides of the queue check;
- PRIMASK restoration, ISR guard, SLEEPDEEP clearing;
- tick rollover and saturating counters.

Existing power_accounting, rtc_monotonic, logger_runtime and button_fsm Python
vector tests also passed. These existing tests are models, not hardware tests.

Release and Debug builds completed without compiler warnings in the rebuilt
files. Sizes from arm-none-eabi-size:

| Build | text | data | bss |
|---|---:|---:|---:|
| Release | 128412 | 520 | 15432 |
| Debug | 163364 | 520 | 15432 |

Release text grew by 168 bytes relative to the pre-patch ELF; data/bss unchanged.
Release flash payload (text + data) is 128932 bytes, about 49.2% of 256 KiB.

## Deployment

ST-Link 37FF71064E5734361D081543 connected under reset at 950 kHz.
Target ID 0x427, 256 KiB internal Flash, reported supply 3.28 V.

Read the full 262144-byte internal Flash before writing:

`backup/pre_usb_idle_20260919_040859/device_internal_flash_before.bin`

SHA256: `E1F9BE51C0FDC380507C3233B77D91D9EA28E9BC517BD681D8E8CB10FB9295CE`

Flashed `Release/TempLogPro.elf`, verified successfully and reset MCU.
Only internal firmware sectors 0..503 were erased by the normal download.
No mass erase, option-byte change, EEPROM write or external Winbond erase.
No LOGGER START/STOP/CLEAR or configuration-write commands were sent.

ELF SHA256: `12AA98BBB51A738586068DC71584EB63A288E9768B15730F2B1049A6CB5A3D07`

BIN SHA256: `DF4C05F0F1832B16CECBC1F1FDEF0A85D08C5DB427856A89AD87B971A0F56784`

Matching BIN and HEX were generated in Release. Pre-patch source and ELF copies
are in the same backup directory. The internal Flash backup is the actual
device image from before this deployment, not an assumption about its version.

## On-device USB Checks

- Composite USB enumerated as COM3 and drive F:.
- PING, GET STATUS, LOGGER STATUS, GET POWER and GET CONFIG responded.
- 100 sequential PINGs: median 0.249 ms, p95 0.420 ms, maximum 9.018 ms measured
  on the host in this run. Not a worst-case latency guarantee.
- One split command and a burst of three commands passed.
- Read all 694915 bytes of TEMPLOGPRO.HTML, interleaving 22 further PINGs.
  Windows may cache disk reads; this is a host-visible compatibility check,
  not a guaranteed uncached USB throughput benchmark.
- Logger remained RUN/LOGGING; observed record count advanced from 15000 to
  15020. Capacity reported 42284 records, interval 5 s, battery_mode=1,
  led_enable=0, thresholds 24.000 and 31.500 degrees C. Settings not changed.
- GET POWER reported USB, 32 MHz, VDD about 3295..3297 mV and an increasing
  idle_entries counter (129572 to 133076); STOP remained zero on USB.

Software telemetry is NOT a current measurement. During the report-read test,
active_ms_est increased by 6084 ms while uptime_ms increased by only 2403 ms.
These counters therefore must not be used as a precise CPU-duty or energy
balance. They use different bases (DWT cycles versus HAL ticks); USB interrupt
load and delayed ticks are a possible cause that needs a separate audit.
No telemetry redesign or USB interrupt-priority change was made here.

## Next Physical Check

1. With power disconnected, remove ST-Link/debug wires before measuring current.
2. Battery only: compare awake-menu current and idle current after at least 10 s.
3. Keep interval, LED and battery-mode settings identical between comparisons.
4. Separately measure the battery-branch current with USB connected. Do not
   interpret total USB current as battery current.
5. Check buttons, persistent status icons and continued records after reconnect.

Use current mode only in series, never across the battery. Zero volts on the
empty battery contact with USB attached does not by itself prove how the board
shares power when both sources are connected.

## User Measurement Follow-up

After deployment the user reported battery measurements unchanged and about
16 mA with USB attached, also unchanged. In the earlier established setup the
USB-connected reading was measured in the battery branch, not at USB input.
No additional wiring change was reported. Earlier reference readings were
about 0.35 mA awake, peaks around 0.5 mA and up to 0.07 mA with the display idle.
The latest message did not give new separate numeric battery readings.

Conclusion: no reduction visible at the meter's resolution in this test. Do
not claim a current saving from the idle update, or infer a specific hardware
fault from the unchanged reading. Existing battery_mode=1 already allowed WFI.

Read-only code review after this result found:

- `templogpro_app_should_stop_sleep()` blocks STOP for the entire
  `APP_MEASURE_PULSE_MS=600` interval, even with led_enable=0. The pulse also
  controls the LCD heart through `app_idle_icon_mask()`. CPU can still use
  shallow WFI during that time; it is not 600 ms of continuous computation.
  With a 5 s measurement interval this guard covers nominally 12% of each
  interval. It is a candidate for reducing average current, not an explanation
  of the minimum current while already in STOP. Simply removing the guard
  would change heart timing with the current 1 s RTC wakeup.
- Sensor initialization only configures the bit-banged I2C pins. Reads use
  register 0x00 at address 0x48. There is no sensor shutdown/configuration write.
  Address and compatible temperature format do not establish the exact sensor
  model. Identify the chip before attempting shutdown or one-shot commands.

No further firmware edits or flashing in this follow-up. Next software work
should preserve visible heartbeat timing and measurement scheduling while
reducing the shallow-sleep interval. Sensor power control requires confirmed
hardware identification. Current telemetry still cannot substitute for a
meter measurement.

Reference for masked WFI wakeup: ST PM0056, section 2.5.2,
https://www.st.com/resource/en/programming_manual/cd00228163.pdf
