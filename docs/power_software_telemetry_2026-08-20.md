# TempLogPro software power telemetry

Date: 2026-08-20

Firmware version: `0.8.12-power`

## Purpose

This diagnostic build adds power-related software counters without requiring
an ammeter. It does not claim to measure battery current or battery capacity.

The same live data is available through:

- the read-only Mass Storage file `POWER.TXT`;
- the CDC command `GET POWER`;
- the Power status button in `TEMPLOGPRO.HTML` Settings.

## Reported values

- detected source: USB or battery;
- MCU supply voltage derived from the internal VREFINT channel;
- minimum and maximum observed MCU supply voltage;
- current system clock;
- firmware uptime;
- time attributed to USB and battery operation;
- estimated CPU-active time and duty ratio;
- shallow `WFI` sleep time and entry count;
- STOP-mode time and entry count;
- RTC and button wakeup counts;
- STOP attempts deferred by button activity or wakeup setup.

VDD conversions are cached for 5 seconds. This avoids repeatedly starting the
ADC whenever the battery page or a USB diagnostic is refreshed.

## Measurement limits

- VREFINT estimates the voltage powering the STM32. It is not a direct
  measurement at the CR3450 terminals.
- Current consumption in microamps cannot be calculated reliably without a
  shunt, current monitor, or ammeter.
- CPU-active time uses the Cortex-M3 DWT cycle counter around the main work
  section. It is an estimate and does not include peripheral current. The DWT
  block is disabled in battery mode, so this estimate accumulates only while
  USB is detected.
- STOP time woken by the 1-second RTC wakeup is counted in 1-second units.
  A shorter button wakeup can therefore be under-reported.
- Counters start at each MCU reset and are not written to Winbond.

## Safety and storage

The change does not erase or rewrite logger records. `POWER.TXT` is generated
in RAM while the host reads it, just like the other virtual USB files.

## Suggested no-meter test

1. Boot on battery and let the logger run for at least 10 minutes.
2. Open `POWER.TXT` and confirm that battery time and STOP wakeups increase.
3. Connect USB and reopen the file after several seconds.
4. Confirm that the source changes to USB, the clock changes to 32 MHz, and USB
   time increases while the existing logger record count remains unchanged.

## Device verification

Verified on the connected STM32L152RCT6 on 2026-08-20:

- STM32CubeProgrammer download and verify passed at 3.29 V;
- USB Mass Storage and CDC re-enumerated after reset;
- `POWER.TXT` appeared as a 1024-byte dynamic read-only file;
- `GET POWER` reported USB source, about 3.30 V VDD and 32 MHz clock;
- logger remained stopped with 8028 records and a 5-second interval;
- `DATA.CSV` SHA-256 stayed identical before and after the firmware update;
- generated `TEMPLOGPRO.HTML` passed JavaScript syntax validation.
